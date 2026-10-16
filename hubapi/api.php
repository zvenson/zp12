<?php
// The hub's accounts: favourites and ratings, signed in without a password (a link or a 6-digit code by mail).
// One script behind /api/ (nginx: fastcgi to this file, in the vibe-php container). Per site (fastcgi_param):
//   HUB_DATA     its own folder (hub.sqlite, secret.txt): the test site and the live one never share accounts
//   HUB_CATALOG  its catalog.json (the firmware ids one may rate)
//   HUB_BASE     its address, for the links in the mails (https://thefirmwarehub.com)
//   HUB_MAILCFG  VIBE's config.php: the SMTP account (zven@thefirmwarehub.com) is borrowed from there; the mails go out
//                as HUB_FROM (no-reply@thefirmwarehub.com: replies and bounces never land in a mailbox someone reads)
// Routes: GET me, GET ratings, POST login, POST code, GET|POST verify, POST logout, POST fav, POST rate, POST merge, POST delete.
// POSTs need the header X-Hub: 1 (a form on another site cannot send it: no CSRF token needed) and JSON.
declare(strict_types=1);

const SESSION_DAYS = 365;               // signed in for a year on a device, a visit renews it
const LOGIN_MINUTES = 15;               // a link / code lasts this long, once
const CODE_TRIES = 5;                   // wrong codes per mail before it is spent
const COOKIE = 'hub_s';
// the box in the sign-in dialog (unticked): its words are kept with the consent, as VIBE's own form does
const VIBE_CONSENT = 'Also put me on the VIBE FM list: news about VIBE FM, the synth made for open firmware. Unsubscribe any time.';

$ENV = [
    'data' => getenv('HUB_DATA') ?: ($_SERVER['HUB_DATA'] ?? ''),
    'catalog' => getenv('HUB_CATALOG') ?: ($_SERVER['HUB_CATALOG'] ?? ''),
    'base' => rtrim(getenv('HUB_BASE') ?: ($_SERVER['HUB_BASE'] ?? ''), '/'),
    'mailcfg' => getenv('HUB_MAILCFG') ?: ($_SERVER['HUB_MAILCFG'] ?? ''),
    'from' => getenv('HUB_FROM') ?: ($_SERVER['HUB_FROM'] ?? 'no-reply@thefirmwarehub.com'),
];

header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');
header('X-Content-Type-Options: nosniff');
header('Referrer-Policy: same-origin');

function out(array $data, int $code = 200): never
{
    http_response_code($code);
    echo json_encode($data, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
    exit;
}

function fail(string $msg, int $code = 400): never
{
    out(['ok' => false, 'error' => $msg], $code);
}

function now(): string
{
    return gmdate('Y-m-d H:i:s');
}

function secret(): string
{
    global $ENV;
    static $s = null;
    if ($s !== null) {
        return $s;
    }
    $f = $ENV['data'] . '/secret.txt';      // (made on the first run, never in git)
    if (!is_file($f)) {
        file_put_contents($f, bin2hex(random_bytes(32)), LOCK_EX);
        chmod($f, 0600);
    }
    return $s = trim((string)file_get_contents($f));
}

function hsh(string $s): string
{
    return hash_hmac('sha256', $s, secret());
}

function client_ip(): string
{
    $ip = $_SERVER['HTTP_CF_CONNECTING_IP'] ?? $_SERVER['REMOTE_ADDR'] ?? '';
    return filter_var($ip, FILTER_VALIDATE_IP) ? $ip : '';
}

function db(): PDO
{
    global $ENV;
    static $pdo = null;
    if ($pdo) {
        return $pdo;
    }
    if ($ENV['data'] === '' || !is_dir($ENV['data'])) {
        fail('not set up', 500);
    }
    $pdo = new PDO('sqlite:' . $ENV['data'] . '/hub.sqlite', null, null, [
        PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
        PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
    ]);
    $pdo->exec('PRAGMA journal_mode = WAL; PRAGMA busy_timeout = 5000; PRAGMA foreign_keys = ON;');
    $pdo->exec("
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY,
            email TEXT NOT NULL UNIQUE,
            created TEXT NOT NULL,
            last_login TEXT
        );
        CREATE TABLE IF NOT EXISTS logins (                  -- a sign-in mail: its link and its code, spent on first use
            id INTEGER PRIMARY KEY,
            email TEXT NOT NULL,
            token TEXT NOT NULL UNIQUE,                     -- hmac of the link's token
            code TEXT NOT NULL,                             -- hmac of id + the 6 digits
            created TEXT NOT NULL,
            expires TEXT NOT NULL,
            used TEXT,
            tries INTEGER NOT NULL DEFAULT 0
        );
        CREATE TABLE IF NOT EXISTS sessions (
            token TEXT PRIMARY KEY,                         -- hmac of the cookie
            user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
            created TEXT NOT NULL,
            seen TEXT NOT NULL,
            expires TEXT NOT NULL
        );
        CREATE TABLE IF NOT EXISTS favs (
            user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
            fw TEXT NOT NULL,
            created TEXT NOT NULL,
            PRIMARY KEY (user_id, fw)
        );
        CREATE TABLE IF NOT EXISTS ratings (
            user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
            fw TEXT NOT NULL,
            stars INTEGER NOT NULL CHECK (stars BETWEEN 1 AND 5),
            created TEXT NOT NULL,
            updated TEXT NOT NULL,
            PRIMARY KEY (user_id, fw)
        );
        CREATE INDEX IF NOT EXISTS ratings_fw ON ratings(fw);
        CREATE TABLE IF NOT EXISTS throttle (k TEXT NOT NULL, t INTEGER NOT NULL);
        CREATE INDEX IF NOT EXISTS throttle_k ON throttle(k, t);
    ");
    // later columns: the VIBE list box (logins.vibe: ticked for this mail; users.vibe_*: confirmed by signing in with it)
    $has = fn(string $tb) => array_column($pdo->query("PRAGMA table_info($tb)")->fetchAll(), 'name');
    if (!in_array('vibe', $has('logins'), true)) {
        $pdo->exec('ALTER TABLE logins ADD COLUMN vibe INTEGER NOT NULL DEFAULT 0');
    }
    if (!in_array('vibe_at', $has('users'), true)) {
        $pdo->exec("ALTER TABLE users ADD COLUMN vibe_at TEXT");                  // (UTC: when the sign-in confirmed it)
        $pdo->exec("ALTER TABLE users ADD COLUMN vibe_text TEXT NOT NULL DEFAULT ''");
        $pdo->exec("ALTER TABLE users ADD COLUMN vibe_sent TEXT");                // (when it went to VIBE's list: live only)
    }
    return $pdo;
}

// at most $max hits of $key in $secs (the key hashed: no IP or address kept in clear); counts this one
function throttle(string $key, int $max, int $secs): bool
{
    $k = hsh($key);
    $t = time();
    db()->prepare('DELETE FROM throttle WHERE t < ?')->execute([$t - 86400]);
    $q = db()->prepare('SELECT COUNT(*) FROM throttle WHERE k = ? AND t > ?');
    $q->execute([$k, $t - $secs]);
    if ((int)$q->fetchColumn() >= $max) {
        return false;
    }
    db()->prepare('INSERT INTO throttle (k, t) VALUES (?, ?)')->execute([$k, $t]);
    return true;
}

// the firmware ids one may rate: those in the site's catalogue
function firmware_ids(): array
{
    global $ENV;
    $c = json_decode((string)@file_get_contents($ENV['catalog']), true);
    return array_column($c['firmwares'] ?? [], 'id');
}

function valid_fw($fw): string
{
    if (!is_string($fw) || !in_array($fw, firmware_ids(), true)) {
        fail('unknown firmware');
    }
    return $fw;
}

function body(): array
{
    $b = json_decode((string)file_get_contents('php://input'), true);
    return is_array($b) ? $b : [];
}

function post_only(): void
{
    if ($_SERVER['REQUEST_METHOD'] !== 'POST' || ($_SERVER['HTTP_X_HUB'] ?? '') !== '1') {
        fail('POST with X-Hub: 1', 405);
    }
    global $ENV;                            // (and from our own pages only, when the browser says where from)
    $origin = $_SERVER['HTTP_ORIGIN'] ?? '';
    if ($origin !== '' && $origin !== $ENV['base']) {
        fail('wrong origin', 403);
    }
}

function user(): ?array
{
    $c = $_COOKIE[COOKIE] ?? '';
    if (!is_string($c) || !preg_match('/^[a-f0-9]{64}$/', $c)) {
        return null;
    }
    $q = db()->prepare('SELECT s.token, s.seen, u.id, u.email FROM sessions s JOIN users u ON u.id = s.user_id
                        WHERE s.token = ? AND s.expires > ?');
    $q->execute([hsh($c), now()]);
    $u = $q->fetch();
    if (!$u) {
        return null;
    }
    if (strtotime($u['seen'] . ' UTC') < time() - 86400) {   // (once a day: a year from today, the cookie too)
        db()->prepare('UPDATE sessions SET seen = ?, expires = ? WHERE token = ?')
            ->execute([now(), gmdate('Y-m-d H:i:s', time() + SESSION_DAYS * 86400), $u['token']]);
        set_cookie($c, time() + SESSION_DAYS * 86400);
    }
    return ['id' => (int)$u['id'], 'email' => $u['email']];
}

function need_user(): array
{
    return user() ?? fail('not signed in', 401);
}

function set_cookie(string $value, int $expires): void
{
    setcookie(COOKIE, $value, ['expires' => $expires, 'path' => '/api/', 'secure' => true, 'httponly' => true, 'samesite' => 'Lax']);
}

function start_session(string $email): void
{
    $now = now();
    db()->prepare('INSERT INTO users (email, created) VALUES (?, ?) ON CONFLICT(email) DO NOTHING')->execute([$email, $now]);
    $q = db()->prepare('SELECT id FROM users WHERE email = ?');
    $q->execute([$email]);
    $uid = (int)$q->fetchColumn();
    db()->prepare('UPDATE users SET last_login = ? WHERE id = ?')->execute([$now, $uid]);
    $token = bin2hex(random_bytes(32));
    $exp = time() + SESSION_DAYS * 86400;
    db()->prepare('INSERT INTO sessions (token, user_id, created, seen, expires) VALUES (?, ?, ?, ?, ?)')
        ->execute([hsh($token), $uid, $now, $now, gmdate('Y-m-d H:i:s', $exp)]);
    set_cookie($token, $exp);
}

// everything about one user, for the page: their favourites and their own ratings
function mine(array $u): array
{
    $f = db()->prepare('SELECT fw FROM favs WHERE user_id = ? ORDER BY created');
    $f->execute([$u['id']]);
    $r = db()->prepare('SELECT fw, stars FROM ratings WHERE user_id = ?');
    $r->execute([$u['id']]);
    $v = db()->prepare('SELECT vibe_at IS NOT NULL FROM users WHERE id = ?');
    $v->execute([$u['id']]);
    return ['email' => $u['email'], 'vibe' => (bool)$v->fetchColumn(), 'favs' => $f->fetchAll(PDO::FETCH_COLUMN), 'ratings' => (object)$r->fetchAll(PDO::FETCH_KEY_PAIR)];
}

// every firmware's ratings: [average, count]
function all_ratings(): array
{
    $l = db()->query('SELECT fw, ROUND(AVG(stars), 2) AS a, COUNT(*) AS n FROM ratings GROUP BY fw')->fetchAll();
    $o = [];
    foreach ($l as $x) {
        $o[$x['fw']] = [(float)$x['a'], (int)$x['n']];
    }
    return $o;
}

function login_mail(string $to, string $link, string $code, bool $vibe = false): bool
{
    global $ENV;
    $vibe = is_file($ENV['mailcfg']) ? require $ENV['mailcfg'] : [];
    $m = $vibe['mail'] ?? null;
    if (!$m) {
        error_log('hub: no mail config');
        return false;
    }
    $host = parse_url($ENV['base'], PHP_URL_HOST) ?: 'thefirmwarehub.com';
    $vibeLine = 'You also ticked the box for the VIBE FM list: signing in confirms it (news about VIBE FM, the synth made for open '
        . 'firmware; one click in each mail unsubscribes). Without signing in, nothing is added.';
    $text = "Your sign-in code for the FM-1 Firmware Hub:\n\n    $code\n\nType it on the page, or open this link on the device "
        . "you want to sign in on:\n$link\n\nBoth work once, for " . LOGIN_MINUTES . " minutes. No password needed: your favourites "
        . "and ratings follow your address to every device.\n\n" . ($vibe ? $vibeLine . "\n\n" : '')
        . "You did not ask for this? Then ignore it: without the code or the link nobody signs in.\n-- \n"
        . "The FM-1 Firmware Hub · $host (this address does not read replies)\n";
    $e = fn(string $s) => htmlspecialchars($s, ENT_QUOTES, 'UTF-8');
    $html = '<!doctype html><html><body style="margin:0;background:#121011;padding:32px 16px;font-family:Arial,Helvetica,sans-serif;'
        . 'color:#efe8e1;font-size:16px;line-height:1.5"><div style="max-width:520px;margin:0 auto">'
        . '<p style="font-size:21px;font-weight:300;margin:0 0 24px">FM-1 <b style="color:#42f5f5;font-weight:800">Firmware Hub</b></p>'
        . '<p style="margin:0 0 10px">Your sign-in code:</p>'
        . '<p style="font:700 34px/1.2 Menlo,Consolas,monospace;letter-spacing:8px;margin:0 0 24px;color:#fff">' . $e($code) . '</p>'
        . '<p style="margin:0 0 14px">Type it on the page, or sign in on this device:</p>'
        . '<p style="margin:0 0 28px"><a href="' . $e($link) . '" style="background:#42f5f5;color:#121011;text-decoration:none;font-weight:700;'
        . 'padding:13px 22px;border-radius:8px;display:inline-block">Sign in</a></p>'
        . '<p style="margin:0 0 14px;color:#a39c96;font-size:14px">Both work once, for ' . LOGIN_MINUTES . ' minutes. No password: your '
        . 'favourites and ratings follow your address to every device. You did not ask for this? Ignore it, nobody signs in without the code or the link.</p>'
        . ($vibe ? '<p style="margin:0 0 14px;color:#ffb27a;font-size:14px">' . $e($vibeLine) . '</p>' : '')
        . '<p style="font-size:12px;color:#6f6863;margin-top:32px;border-top:1px solid #2a2627;padding-top:16px">The FM-1 Firmware Hub · '
        . $e($host) . '</p></div></body></html>';
    return send_mail(['from' => $ENV['from']] + $m, $to, "Your sign-in code: $code", $text, $html, $host);
}

// a small SMTP client (as VIBE's mailer.php: STARTTLS on 587, AUTH LOGIN)
function send_mail(array $m, string $to, string $subject, string $text, string $html, string $helo): bool
{
    $from = $m['from'];
    $boundary = 'b' . bin2hex(random_bytes(8));
    $headers = [
        'Date: ' . date(DATE_RFC2822),
        'From: "FM-1 Firmware Hub" <' . $from . '>',
        'To: <' . $to . '>',
        'Subject: ' . $subject,
        'Message-ID: <' . bin2hex(random_bytes(12)) . '@' . substr(strrchr($from, '@') ?: '@localhost', 1) . '>',
        'MIME-Version: 1.0',
        'Auto-Submitted: auto-generated',
        'Content-Type: multipart/alternative; boundary="' . $boundary . '"',
    ];
    $part = fn(string $type, string $b) => "--$boundary\r\nContent-Type: $type; charset=UTF-8\r\n"
        . "Content-Transfer-Encoding: quoted-printable\r\n\r\n" . quoted_printable_encode(str_replace("\n", "\r\n", $b)) . "\r\n";
    $msg = implode("\r\n", $headers) . "\r\n\r\n" . $part('text/plain', $text) . $part('text/html', $html) . "--$boundary--\r\n";
    if (($m['transport'] ?? 'log') !== 'smtp') {
        global $ENV;
        return file_put_contents($ENV['data'] . '/mail.log', "==== " . now() . "\n$msg\n", FILE_APPEND | LOCK_EX) !== false;
    }
    try {
        $ssl = ($m['secure'] ?? '') === 'ssl';
        $ctx = stream_context_create(['ssl' => ['peer_name' => $m['host'], 'verify_peer' => true, 'verify_peer_name' => true]]);
        $s = stream_socket_client(($ssl ? 'ssl://' : 'tcp://') . $m['host'] . ':' . (int)$m['port'], $errno, $err, 15, STREAM_CLIENT_CONNECT, $ctx);
        if (!$s) {
            throw new RuntimeException("connect: $err");
        }
        stream_set_timeout($s, 20);
        $cmd = function (?string $c, int $want) use ($s): void {
            if ($c !== null) {
                fwrite($s, $c . "\r\n");
            }
            $all = '';
            while (($line = fgets($s, 1024)) !== false) {
                $all .= $line;
                if (strlen($line) < 4 || $line[3] === ' ') {
                    break;
                }
            }
            if ((int)substr($all, 0, 3) !== $want) {
                throw new RuntimeException(trim(($c !== null ? strtok($c, ' ') . ': ' : '') . $all));
            }
        };
        $cmd(null, 220);
        $cmd("EHLO $helo", 250);
        if (($m['secure'] ?? '') === 'tls') {
            $cmd('STARTTLS', 220);
            if (!stream_socket_enable_crypto($s, true, STREAM_CRYPTO_METHOD_TLSv1_2_CLIENT | STREAM_CRYPTO_METHOD_TLSv1_3_CLIENT)) {
                throw new RuntimeException('STARTTLS failed');
            }
            $cmd("EHLO $helo", 250);
        }
        if (($m['user'] ?? '') !== '') {
            $cmd('AUTH LOGIN', 334);
            $cmd(base64_encode($m['user']), 334);
            $cmd(base64_encode($m['pass']), 235);
        }
        $cmd("MAIL FROM:<$from>", 250);
        $cmd("RCPT TO:<$to>", 250);
        $cmd('DATA', 354);
        $cmd(preg_replace('/^\./m', '..', $msg) . "\r\n.", 250);
        fwrite($s, "QUIT\r\n");
        fclose($s);
        return true;
    } catch (Throwable $e) {
        error_log('hub mail failed: ' . $e->getMessage());
        return false;
    }
}

// the page the mail's link opens: a button, not an instant sign-in (mail scanners open links on their own; a button
// they do not press, so the link is still good when its owner comes)
function verify_page(string $token, string $msg = ''): never
{
    global $ENV;
    header('Content-Type: text/html; charset=utf-8');
    header('X-Robots-Tag: noindex');
    $e = fn(string $s) => htmlspecialchars($s, ENT_QUOTES, 'UTF-8');
    echo '<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">'
        . '<meta name="robots" content="noindex"><title>Sign in · FM-1 Firmware Hub</title><style>'
        . 'body{margin:0;min-height:100vh;display:grid;place-items:center;background:#121011;color:#efe8e1;font:16px/1.6 Inter,-apple-system,Segoe UI,Roboto,Arial,sans-serif;padding:16px}'
        . '.box{max-width:420px;width:100%;background:#1b1819;border:1px solid #2f2a2b;border-radius:14px;padding:30px 26px;text-align:center}'
        . 'h1{font-size:21px;font-weight:300;margin:0 0 18px}h1 b{font-weight:800;color:#42f5f5}'
        . 'button,a.b{display:inline-block;background:#42f5f5;color:#121011;border:0;border-radius:10px;padding:13px 24px;font-family:inherit;font-weight:700;font-size:16px;cursor:pointer;text-decoration:none}'
        . 'p{color:#c9c0b8}</style></head><body><div class="box"><h1>FM-1 <b>Firmware Hub</b></h1>';
    if ($msg !== '') {
        echo '<p>' . $e($msg) . '</p><p><a class="b" href="' . $e($ENV['base']) . '/">To the hub</a></p>';
    } else {
        echo '<p>Sign in on this device?</p><form method="post" action="verify"><input type="hidden" name="t" value="' . $e($token) . '">'
            . '<button>Sign in</button></form>';
    }
    echo '</div></body></html>';
    exit;
}

// a login (link or code) spent: the session, then the page knows
function use_login(array $l): void
{
    db()->prepare('UPDATE logins SET used = ? WHERE id = ?')->execute([now(), $l['id']]);
    start_session($l['email']);
    if ((int)($l['vibe'] ?? 0)) {                       // (the mail said so: signing in with it is the double opt-in)
        db()->prepare('UPDATE users SET vibe_at = ?, vibe_text = ? WHERE email = ? AND vibe_at IS NULL')
            ->execute([now(), VIBE_CONSENT, $l['email']]);
    }
}

$route = preg_replace('~^.*/api/~', '', (string)parse_url($_SERVER['REQUEST_URI'] ?? '', PHP_URL_PATH));
$ip = client_ip();

switch ($route) {
    case 'me':                                          // who is signed in on this device (null: nobody) + all ratings
        $u = user();
        out(['ok' => true, 'user' => $u ? mine($u) : null, 'ratings' => (object)all_ratings()]);

    case 'ratings':
        out(['ok' => true, 'ratings' => (object)all_ratings()]);

    case 'login':                                       // an address: a mail with a link and a code
        post_only();
        $b = body();
        if (!empty($b['website'])) {                    // (the honeypot: a bot filled the hidden field; it looks sent)
            out(['ok' => true]);
        }
        $email = strtolower(trim((string)($b['email'] ?? '')));
        if (strlen($email) > 190 || !filter_var($email, FILTER_VALIDATE_EMAIL)) {
            fail('Please check the address.');
        }
        if (!throttle("login-ip:$ip", 10, 3600) || !throttle("login-mail:$email", 3, 900) || !throttle("login-mail-day:$email", 8, 86400)) {
            fail('Too many sign-in mails. Please wait a few minutes and look in your inbox (and spam folder).', 429);
        }
        $token = bin2hex(random_bytes(24));
        $code = str_pad((string)random_int(0, 999999), 6, '0', STR_PAD_LEFT);
        $vibe = !empty($b['vibe']);                     // (the VIBE list box: unticked unless the visitor ticks it)
        db()->prepare('INSERT INTO logins (email, token, code, created, expires, vibe) VALUES (?, ?, ?, ?, ?, ?)')
            ->execute([$email, hsh($token), '', now(), gmdate('Y-m-d H:i:s', time() + LOGIN_MINUTES * 60), (int)$vibe]);
        $id = (int)db()->lastInsertId();
        db()->prepare('UPDATE logins SET code = ? WHERE id = ?')->execute([hsh("$id:$code"), $id]);
        if (!login_mail($email, $ENV['base'] . '/api/verify?t=' . $token, $code, $vibe)) {
            fail('The mail could not be sent. Please try again later.', 502);
        }
        out(['ok' => true]);

    case 'code':                                        // the 6 digits from the mail, typed on any device
        post_only();
        $b = body();
        $email = strtolower(trim((string)($b['email'] ?? '')));
        $code = preg_replace('/\D/', '', (string)($b['code'] ?? ''));
        if (!throttle("code-ip:$ip", 30, 3600)) {
            fail('Too many tries. Please wait a while.', 429);
        }
        $q = db()->prepare('SELECT * FROM logins WHERE email = ? AND used IS NULL AND expires > ? AND tries < ? ORDER BY id DESC LIMIT 1');
        $q->execute([$email, now(), CODE_TRIES]);
        $l = $q->fetch();
        if (!$l || strlen($code) !== 6) {
            fail('This code is not valid (any more). Ask for a new mail.');
        }
        if (!hash_equals($l['code'], hsh($l['id'] . ':' . $code))) {
            db()->prepare('UPDATE logins SET tries = tries + 1 WHERE id = ?')->execute([$l['id']]);
            $left = CODE_TRIES - (int)$l['tries'] - 1;
            fail($left > 0 ? "Wrong code. $left " . ($left === 1 ? 'try' : 'tries') . ' left.' : 'Wrong code. Ask for a new mail.');
        }
        use_login($l);
        out(['ok' => true, 'user' => mine(need_user_after_login($l['email']))]);

    case 'verify':                                      // the mail's link: a button (GET), then the sign-in (POST)
        $token = (string)($_POST['t'] ?? $_GET['t'] ?? '');
        $q = db()->prepare('SELECT * FROM logins WHERE token = ? AND used IS NULL AND expires > ?');
        $q->execute([hsh($token), now()]);
        $l = $q->fetch();
        if (!$l) {
            verify_page('', 'This link was used already or is older than ' . LOGIN_MINUTES . ' minutes. On the hub, ask for a new one.');
        }
        if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
            verify_page($token);
        }
        use_login($l);
        header('Location: ' . $ENV['base'] . '/?signedin=1', true, 303);
        exit;

    case 'logout':
        post_only();
        $c = $_COOKIE[COOKIE] ?? '';
        if (is_string($c) && $c !== '') {
            db()->prepare('DELETE FROM sessions WHERE token = ?')->execute([hsh($c)]);
        }
        set_cookie('', 1);
        out(['ok' => true]);

    case 'fav':                                         // {fw, on}
        post_only();
        $u = need_user();
        $b = body();
        $fw = valid_fw($b['fw'] ?? null);
        if (!empty($b['on'])) {
            db()->prepare('INSERT OR IGNORE INTO favs (user_id, fw, created) VALUES (?, ?, ?)')->execute([$u['id'], $fw, now()]);
        } else {
            db()->prepare('DELETE FROM favs WHERE user_id = ? AND fw = ?')->execute([$u['id'], $fw]);
        }
        out(['ok' => true, 'user' => mine($u)]);

    case 'rate':                                        // {fw, stars: 1..5, 0 = take mine back}
        post_only();
        $u = need_user();
        $b = body();
        $fw = valid_fw($b['fw'] ?? null);
        $stars = (int)($b['stars'] ?? -1);
        if ($stars < 0 || $stars > 5) {
            fail('1 to 5 stars');
        }
        if (!throttle("rate:{$u['id']}", 60, 600)) {
            fail('Slow down a little.', 429);
        }
        if ($stars === 0) {
            db()->prepare('DELETE FROM ratings WHERE user_id = ? AND fw = ?')->execute([$u['id'], $fw]);
        } else {
            db()->prepare('INSERT INTO ratings (user_id, fw, stars, created, updated) VALUES (?, ?, ?, ?, ?)
                           ON CONFLICT(user_id, fw) DO UPDATE SET stars = excluded.stars, updated = excluded.updated')
                ->execute([$u['id'], $fw, $stars, now(), now()]);
        }
        out(['ok' => true, 'user' => mine($u), 'ratings' => (object)all_ratings()]);

    case 'merge':                                       // {favs: [...]}: the favourites set before signing in, into the account
        post_only();
        $u = need_user();
        $ids = firmware_ids();
        $add = db()->prepare('INSERT OR IGNORE INTO favs (user_id, fw, created) VALUES (?, ?, ?)');
        foreach (array_slice((array)(body()['favs'] ?? []), 0, 200) as $fw) {
            if (is_string($fw) && in_array($fw, $ids, true)) {
                $add->execute([$u['id'], $fw, now()]);
            }
        }
        out(['ok' => true, 'user' => mine($u)]);

    case 'delete':                                      // the account, its favourites, ratings and sessions: gone
        post_only();
        $u = need_user();
        db()->prepare('DELETE FROM logins WHERE email = ?')->execute([$u['email']]);
        db()->prepare('DELETE FROM users WHERE id = ?')->execute([$u['id']]);
        set_cookie('', 1);
        out(['ok' => true]);

    default:
        fail('not found', 404);
}

function need_user_after_login(string $email): array
{
    $q = db()->prepare('SELECT id, email FROM users WHERE email = ?');
    $q->execute([$email]);
    $u = $q->fetch();
    return ['id' => (int)$u['id'], 'email' => $u['email']];
}
