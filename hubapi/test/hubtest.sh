#!/bin/sh
# runs inside vibe-php: api.php on php -S with its own data in /tmp, mails into a log; walks the whole sign-in
set -u
D=/tmp/hubtest; rm -rf $D; mkdir -p $D/data
echo "<?php return ['mail' => ['transport' => 'log', 'from' => 'test@example.invalid']];" > $D/mail.php
cp /srv/staging-live/catalog.json $D/catalog.json
cat > $D/router.php <<'R'
<?php require '/srv/hub-staging/api.php';
R
B=http://127.0.0.1:8098
HUB_DATA=$D/data HUB_CATALOG=$D/catalog.json HUB_BASE=$B HUB_MAILCFG=$D/mail.php php -S 127.0.0.1:8098 $D/router.php >$D/srv.log 2>&1 &
PID=$!; sleep 1
ok=0; bad=0
chk() { if echo "$2" | grep -q -- "$3"; then ok=$((ok+1)); echo "ok   $1"; else bad=$((bad+1)); echo "FAIL $1: $2"; fi; }
J='Content-Type: application/json'
P() { curl -s -b $D/jar -c $D/jar -H "$J" -H 'X-Hub: 1' -d "$2" $B/api/$1; }
chk "me as guest" "$(curl -s $B/api/me)" '"user":null'
chk "POST without X-Hub" "$(curl -s -H "$J" -d '{"email":"a@b.de"}' $B/api/login)" 'X-Hub'
chk "foreign origin" "$(curl -s -H "$J" -H 'X-Hub: 1' -H 'Origin: https://evil.example' -d '{"email":"a@b.de"}' $B/api/login)" 'wrong origin'
chk "bad address" "$(P login '{"email":"nope"}')" 'check the address'
chk "fav as guest" "$(P fav '{"fw":"zp12","on":1}')" 'not signed in'
chk "honeypot" "$(P login '{"email":"bot@example.org","website":"x"}')" '"ok":true'
chk "no mail for the bot" "$(grep -c bot@example.org $D/data/mail.log 2>/dev/null || echo 0)" '^0$'
chk "login mail" "$(P login '{"email":"Tester@Example.org"}')" '"ok":true'
CODE=$(grep -o 'Subject: Your sign-in code: [0-9]*' $D/data/mail.log | tail -1 | grep -o '[0-9]*$')
chk "code in mail" "$CODE" '^[0-9]\{6\}$'
chk "wrong code" "$(P code "{\"email\":\"tester@example.org\",\"code\":\"$(printf %06d $(( (10#$CODE + 1) % 1000000 )) 2>/dev/null || echo 000000)\"}")" 'Wrong code. 4 tries'
chk "right code" "$(P code "{\"email\":\"tester@example.org\",\"code\":\"$CODE\"}")" '"email":"tester@example.org"'
chk "code spent" "$(curl -s -H "$J" -H 'X-Hub: 1' -d "{\"email\":\"tester@example.org\",\"code\":\"$CODE\"}" $B/api/code)" 'not valid'
chk "cookie flags" "$(grep hub_s $D/jar)" 'hub_s'
chk "me signed in" "$(curl -s -b $D/jar $B/api/me)" '"email":"tester@example.org"'
chk "fav on" "$(P fav '{"fw":"zp12","on":1}')" '"favs":\["zp12"\]'
chk "fav unknown fw" "$(P fav '{"fw":"nothere","on":1}')" 'unknown firmware'
chk "merge" "$(P merge '{"favs":["felucca","zp12","bogus"]}' | grep -o '"favs":\[[^]]*\]' | tr , '\n' | grep -c 'felucca\|zp12')" '^2$'
chk "rate 4" "$(P rate '{"fw":"zp12","stars":4}')" '"zp12":\[4,1\]'
chk "SMK-37: own id" "$(P rate '{"fw":"smk37:obervave","stars":5}')" '"smk37:obervave":\[5,1\]'
chk "SMK-37: unknown id" "$(P fav '{"fw":"smk37:nothere","on":1}')" 'unknown firmware'
chk "SMK-37: the FM-1 id stays apart" "$(curl -s $B/api/ratings)" '"zp12":\[4,1\],"smk37:obervave"\|"smk37:obervave":\[5,1\],"zp12":\[4,1\]'
P rate '{"fw":"smk37:obervave","stars":0}' >/dev/null
chk "rate 6" "$(P rate '{"fw":"zp12","stars":6}')" '1 to 5'
chk "public ratings" "$(curl -s $B/api/ratings)" '"zp12":\[4,1\]'
chk "no address in public" "$(curl -s $B/api/ratings | grep -c tester || true)" '^0$'
chk "unrate" "$(P rate '{"fw":"zp12","stars":0}')" '"ratings":{}'
# the link: GET shows a button and spends nothing, POST signs in
P login '{"email":"link@example.org"}' >/dev/null
T=$(php -r 'echo quoted_printable_decode(file_get_contents($argv[1]));' $D/data/mail.log | grep -o 'verify?t=[a-f0-9]*' | tail -1 | cut -d= -f2)
chk "token in mail" "$T" '^[a-f0-9]\{48\}$'
chk "link GET: button" "$(curl -s "$B/api/verify?t=$T")" 'Sign in on this device'
chk "link GET twice still good" "$(curl -s "$B/api/verify?t=$T")" 'Sign in on this device'
chk "link POST" "$(curl -s -i -c $D/jar2 -d "t=$T" $B/api/verify | tr -d '\r' | grep -i '^location')" 'signedin=1'
chk "link spent" "$(curl -s "$B/api/verify?t=$T")" 'used already'
chk "link session" "$(curl -s -b $D/jar2 $B/api/me)" '"email":"link@example.org"'
chk "logout" "$(curl -s -b $D/jar2 -c $D/jar2 -H "$J" -H 'X-Hub: 1' -d '{}' $B/api/logout)" '"ok":true'
chk "after logout" "$(curl -s -b $D/jar2 $B/api/me)" '"user":null'
chk "throttle: 3 mails / 15 min" "$(P login '{"email":"link@example.org"}'; P login '{"email":"link@example.org"}'; P login '{"email":"link@example.org"}')" 'Too many'
chk "delete" "$(P delete '{}')" '"ok":true'
chk "deleted: guest" "$(curl -s -b $D/jar $B/api/me)" '"user":null'
chk "deleted: no rows" "$(php -r "\$p=new PDO('sqlite:$D/data/hub.sqlite');echo \$p->query(\"SELECT COUNT(*) FROM favs\")->fetchColumn(), \$p->query(\"SELECT COUNT(*) FROM users WHERE email='tester@example.org'\")->fetchColumn();")" '^00$'
chk "no PHP warnings" "$(grep -ci 'warning\|fatal\|notice' $D/srv.log || true)" '^0$'
kill $PID; rm -rf $D
echo "== $ok ok, $bad failed"
