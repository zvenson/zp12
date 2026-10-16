# hubapi tests

Both run inside the `vibe-php` container on the server and touch nothing real: the hub runs on `php -S` with its data in
`/tmp`, mails go into a log, VIBE works on a copy of its list (`VACUUM INTO`, `VIBE_CONFIG` with `data_dir` in `/tmp`).

    scp hubapi/test/hubtest.sh hetzner-fm1:/tmp/ && ssh hetzner-fm1 'docker cp /tmp/hubtest.sh vibe-php:/tmp/ && docker exec vibe-php sh /tmp/hubtest.sh'
    scp hubapi/test/synctest.sh hetzner-fm1:/tmp/ && ssh hetzner-fm1 'docker cp /tmp/synctest.sh vibe-php:/tmp/ && docker exec vibe-php sh /tmp/synctest.sh'

- `hubtest.sh`: sign-in by code and by link, guards (X-Hub, origin, honeypot, throttle), favourites, ratings, logout, delete (35 checks)
- `synctest.sh`: the VIBE box: the mail's sentence only when ticked, the proof of consent, VIBE's `app/hub_sync.php` into the
  list copy (new, pending, already confirmed, unsubscribed and back), notify mails, the test site's data refused

Both use `/srv/hub-staging/api.php` (the staging branch on the server): after go-live point them at `/srv/hub/api.php`.
Afterwards: `docker exec -u 0 vibe-php rm -rf /tmp/hubtest* /tmp/synctest*`.
