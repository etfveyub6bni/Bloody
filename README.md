# Bloody Auth Panel

Netlify hosts the static admin panel in `public/` and the API in `netlify/functions/`.
Set `BLOODY_ADMIN_PASSWORD` in the Netlify environment before deployment; the API
rejects admin requests if it is unset. Keys are stored in Netlify Blobs. On a new
store, sign in and create new access codes from the panel.

`npm run dev` starts a static UI preview on port 3000. It does not emulate the
Netlify API or its persistent store; use a Netlify environment to test live
authentication and key changes.
