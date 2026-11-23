# Security

To report a security problem, use **Report a vulnerability** on the repository's [Security tab](https://github.com/Kaepora-HQ/kaepora/security). The report stays private until it is fixed.

Do not open a public issue for it.

Kaepora reads one file from the internet: the newest release's `latest.json` on GitHub, to say when a newer version is out. It asks each time it opens with **Check for new versions** ticked, and when you tick that box.

Kaepora sends a record of a seed only when you complete the seed and click **Record it**. It sends one request to `https://api.kaepora.org/v1/seeds/<id>/completion`. The id is random, belongs to your copy of that seed, and holds the time it was made. The request holds the seed's text, its settings, the version of Kaepora that made it and your play time. If the request fails, Kaepora keeps it on your computer and sends it again the next time it opens. Kaepora puts no address, name, path or id of your computer or of your install in the request. Like any request, it reaches the site from your internet address. Kaepora's server does not store or log that address. The site's host, Vercel, sees it and may keep it in its own logs.

A problem in a download, in these requests, or in what Kaepora writes into the emulator's mods folder is worth a report.
