#!/bin/sh
# inside vibe-php: the hub (php -S, a "live-like" data folder in /tmp) + VIBE on a COPY of its list (VIBE_CONFIG: data_dir
# /tmp/vtest, mails into a log); hub_sync from hub to the copy. The real list and the real hub data are only read.
set -u
D=/tmp/synctest; rm -rf $D; mkdir -p $D/hub/live $D/vtest
php -r '$s = new PDO("sqlite:/srv/vibe/data/vibe.sqlite"); $s->exec("VACUUM INTO \"/tmp/synctest/vtest/vibe.sqlite\"");'
cat > $D/vcfg.php <<'C'
<?php $c = require '/srv/vibe/app/config.php'; $c['data_dir'] = '/tmp/synctest/vtest'; $c['mail']['transport'] = 'log'; return $c;
C
echo "<?php return ['mail' => ['transport' => 'log', 'from' => 'x@example.invalid']];" > $D/hubmail.php
cp /srv/staging-live/catalog.json $D/catalog.json
echo "<?php require '/srv/hub-staging/api.php';" > $D/router.php
B=http://127.0.0.1:8097
HUB_DATA=$D/hub/live HUB_CATALOG=$D/catalog.json HUB_BASE=$B HUB_MAILCFG=$D/hubmail.php php -S 127.0.0.1:8097 $D/router.php >$D/srv.log 2>&1 &
PID=$!; sleep 1
ok=0; bad=0
chk() { if echo "$2" | grep -q -- "$3"; then ok=$((ok+1)); echo "ok   $1"; else bad=$((bad+1)); echo "FAIL $1: $2"; fi; }
V() { VIBE_CONFIG=$D/vcfg.php php -r "require '/srv/vibe/app/bootstrap.php'; $1"; }
signin() {   # $1 address, $2 vibe true|false
  curl -s -H 'Content-Type: application/json' -H 'X-Hub: 1' -d "{\"email\":\"$1\",\"vibe\":$2}" $B/api/login >/dev/null
  CODE=$(grep -o 'Subject: Your sign-in code: [0-9]*' $D/hub/live/mail.log | tail -1 | grep -o '[0-9]*$')
  curl -s -H 'Content-Type: application/json' -H 'X-Hub: 1' -H 'CF-IPCountry: DE' -d "{\"email\":\"$1\",\"code\":\"$CODE\"}" $B/api/code
}
SYNC() { VIBE_CONFIG=$D/vcfg.php php /srv/vibe/app/hub_sync.php $D/hub/live/hub.sqlite; }
N0=$(V 'echo db()->query("SELECT COUNT(*) FROM subscribers")->fetchColumn();')
V 'db()->exec("INSERT INTO subscribers (email, lang, source, token, survey_token, consent_text, created_at, created_ip) VALUES (\"pend@example.org\", \"de\", \"test\", \"a1\", \"a2\", \"x\", \"2026-10-16 09:00:00\", \"\")");
   db()->exec("INSERT INTO subscribers (email, lang, source, status, token, survey_token, consent_text, created_at, created_ip, confirmed_at) VALUES (\"conf@example.org\", \"en\", \"test\", \"confirmed\", \"b1\", \"b2\", \"x\", \"2026-10-01 09:00:00\", \"\", \"2026-10-01 09:05:00\")");'
chk "sign-in with box" "$(signin new@example.org true)" '"vibe":true'
chk "sign-in without box" "$(signin nobox@example.org false)" '"vibe":false'
signin pend@example.org true >/dev/null; signin conf@example.org true >/dev/null
MAILS='$m = preg_split("/^==== /m", quoted_printable_decode(file_get_contents("/tmp/synctest/hub/live/mail.log")), -1, PREG_SPLIT_NO_EMPTY);'
chk "3 mails with the list sentence (the ticked ones so far)" "$(php -r "$MAILS"' echo count(array_filter($m, fn($x) => str_contains($x, "confirms it")));')" '^3$'
chk "the mail without box: no sentence" "$(php -r "$MAILS"' foreach ($m as $x) if (str_contains($x, "To: <nobox@")) echo str_contains($x, "confirms it") ? "has" : "clean";')" '^clean$'
chk "hub keeps the proof" "$(php -r '$h=new PDO("sqlite:/tmp/synctest/hub/live/hub.sqlite"); echo implode("|", $h->query("SELECT vibe_country, vibe_ip<>\"\" , vibe_text<>\"\" FROM users WHERE email=\"new@example.org\"")->fetch(PDO::FETCH_NUM));')" '^DE|1|1$'
chk "staging refused" "$(VIBE_CONFIG=$D/vcfg.php php /srv/vibe/app/hub_sync.php /srv/hubdata/staging/hub.sqlite 2>&1; echo "exit $?")" 'exit 2'
OUT=$(SYNC)
chk "sync: new added" "$OUT" 'new@example.org added'
chk "sync: pending confirmed" "$OUT" 'pend@example.org added'
chk "sync: confirmed untouched" "$OUT" 'conf@example.org already on the list'
chk "sync: no box, not on the list" "$(V '$q=db()->query("SELECT COUNT(*) FROM subscribers WHERE email=\"nobox@example.org\""); echo $q->fetchColumn();')" '^0$'
chk "new row" "$(V 'echo implode("|", db()->query("SELECT status, source, lang, country, confirmed_at IS NOT NULL, consent_text LIKE \"Also put me%\" FROM subscribers WHERE email=\"new@example.org\"")->fetch(PDO::FETCH_NUM));')" '^confirmed|hub-account|en|DE|1|1$'
chk "pending now confirmed (source kept)" "$(V 'echo implode("|", db()->query("SELECT status, source FROM subscribers WHERE email=\"pend@example.org\"")->fetch(PDO::FETCH_NUM));')" '^confirmed|test$'
chk "2 notify mails" "$(grep -c '^Subject: VIBE FM: #' $D/vtest/mail.log)" '^2$'
chk "notify to vibe@, from no-reply" "$(grep -m1 -A3 '^Date:' $D/vtest/mail.log | tr '\n' ' ')" 'no-reply@thefirmwarehub.com.*vibe@thefirmwarehub.com'
chk "second run: nothing" "$(SYNC | wc -l)" '^0$'
chk "list grew by 3" "$(V 'echo db()->query("SELECT COUNT(*) FROM subscribers")->fetchColumn();')" "^$((N0 + 3))$"
# unsubscribed (row gone), ticks again: back on the list
V 'db()->exec("DELETE FROM subscribers WHERE email=\"new@example.org\"");'
signin new@example.org true >/dev/null
chk "after unsubscribe, a new tick" "$(SYNC)" 'new@example.org added'
signin new@example.org false >/dev/null
chk "sign-in without box: no new consent" "$(SYNC | wc -l)" '^0$'
chk "no PHP warnings" "$(grep -ci 'warning\|fatal\|deprecated' $D/srv.log || true)" '^0$'
php -r "$MAILS"' foreach ($m as $x) { preg_match("/^To: (.*)$/m", $x, $to); echo trim($to[1] ?? "?"), " ", str_contains($x, "confirms it") ? "HAS" : "clean", "\n"; }'
kill $PID; rm -rf $D
echo "== $ok ok, $bad failed"
