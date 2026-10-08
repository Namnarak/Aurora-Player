# Aurora Privacy Policy

**Effective date:** 8 September 2026 · **Last reviewed:** 8 September 2026

This policy describes privacy behavior added by the Aurora project. It does not
replace the Roblox Privacy and Cookie Policy or the policies of other services
that Aurora or the Roblox client communicates with.

## Privacy baseline

Aurora follows these project-level rules:

- **No Aurora-operated analytics or behavioral telemetry service by default.**
- **No sale of user data and no Aurora advertising profile.**
- **No automatic upload of Aurora support bundles.** Support bundles are local
  files unless the user deliberately shares them.
- **No silent credential import from Sober, Vinegar, arbitrary browsers, or
  unrelated applications by Aurora Player.**
- **Roblox crash-report upload is disabled by mandatory Aurora runtime policy.**
- **Optional integrations must be opt-in.** Discord Rich Presence is disabled
  by default.
- New Aurora-owned telemetry must not be introduced silently. It requires an
  explicit user-facing opt-in, documentation of the data and destination, and
  an update to this policy before release.

These rules do not mean that Roblox itself is telemetry-free. The official
Roblox client and Roblox services can collect information under Roblox's own
privacy policy.

## 1. Data Aurora stores locally

Depending on enabled features, Aurora can store local data such as:

- runtime configuration and user-selected settings;
- window state, graphics settings, shader caches, and compatibility caches;
- verified Roblox payload metadata, package hashes, signing information, and
  rollback state;
- Aurora session logs;
- sanitized local support bundles after failures;
- Roblox session credentials required to keep the user signed in;
- Roblox browser-tracker state required by supported client behavior;
- Aurora Studio settings and its isolated Studio prefix when Studio is used.

Credential and private diagnostic files are intended to use user-private file
permissions. Users should still protect their home directory and account.

Session logs remain local until the user deletes them. Failure support bundles
are stored locally and Aurora currently prunes old bundles so that only a small
recent set is retained. Aurora does not automatically send these files to the
project maintainers.

## 2. Roblox account credentials

Aurora Player processes Roblox credentials only to provide Roblox login and
session functionality. Credentials must not be placed in public logs or
support bundles.

Aurora Player does not use Sober or Vinegar credential files as a fallback and
does not silently import credentials from arbitrary web browsers. A user who
explicitly provides a development credential override is responsible for that
development configuration.

## 3. Network communication

### Roblox

Normal gameplay necessarily communicates directly with Roblox services. This
can include authentication, account/session APIs, client settings, experience
services, content delivery, browser-tracker initialization, voice or other
features selected by the user, and network requests made by the Roblox client
itself.

Roblox's privacy policy states that Roblox may collect information including
browser or device type, IP address/device identifiers, operating system, and
information about how users interact with Roblox. As a result, using Aurora
should **not** be treated as a way to hide Linux or the user's device/network
characteristics from Roblox.

Roblox's current policy is published at:

- https://en.help.roblox.com/hc/en-us/articles/115004630823-Roblox-Privacy-and-Cookie-Policy

### Roblox package/update sources

Aurora can contact third-party package sources to locate and download an
officially signed Roblox Android package. A provider can observe ordinary
network information such as the source IP address and request metadata under
that provider's own privacy policy. Aurora's package-verification checks do not
make the provider a trusted source of executable identity; the Roblox signing
trust is verified separately.

### Discord Rich Presence

Discord Rich Presence is optional and disabled by default. If the user enables
it, Aurora can communicate presence information to the local Discord client
and can fetch limited Roblox place metadata needed to render that presence.
Discord's own privacy terms then apply to Discord-side processing.

Aurora Studio has a separate opt-in switch. When enabled, it shares only that
Roblox Studio is running with the local Discord client; it does not read Roblox
account credentials or publish place-file contents.

### Project websites and repositories

Opening Aurora project websites, package repositories, or Git hosting uses
those services directly. Their operators may receive normal web request data.
This is separate from Aurora Player analytics; Aurora does not operate a hidden
analytics channel through those services.

## 4. Crash reports and diagnostics

Aurora applies a mandatory runtime policy that disables Roblox crash-report
uploads through the Android crash-report/Backtrace settings known to the
supported payload. This policy is intended to prevent an accidental Android
crash-upload path from becoming active merely because the compatibility layer
implements more of Android in a future release.

Aurora's own failure diagnostics are local. The support-bundle generator
allowlists diagnostic lines and excludes lines containing common sensitive
values such as `.ROBLOSECURITY`, authorization headers, authentication tickets,
and URLs. Users should still review a bundle before sharing it.

Aurora cannot guarantee that every network request made internally by the
Roblox client is a crash report or telemetry request, and this policy does not
claim to block all data collection performed by Roblox.

## 5. Optional microphone, voice, and input features

Microphone/voice functionality is enabled only according to Aurora's runtime
permission/configuration policy and Roblox feature state. Audio, input, game
activity, moderation, and voice data handled by Roblox are governed by Roblox's
privacy policy, not by this Aurora policy.

## 6. Data Aurora does not intentionally collect for the project

Aurora does not intentionally collect for Aurora Project analytics:

- browsing history outside the explicit Roblox web/login flow;
- files from unrelated applications;
- passwords for unrelated services;
- Sober/Vinegar account databases;
- personal documents from the user's home directory;
- advertising identifiers for an Aurora advertising system.

If a future feature requires new project-owned collection, it must follow the
opt-in and policy-update rule in the Privacy baseline above.

## 7. User control and deletion

Aurora's project-owned state is primarily local. Users can remove Aurora local
configuration, cache, logs, support bundles, and stored credentials using their
normal filesystem/account controls or by uninstalling/removing Aurora data.
Deleting Aurora data does not delete information already processed by Roblox,
Discord, package providers, Git hosting, or another third party. Requests about
third-party data must be directed to that third party.

## 8. Security

Aurora's production path uses package/signature verification, payload hashes,
compatibility approval, and canary checks to reduce the risk of running a
modified or unexpected Roblox payload. These controls protect the local Aurora
runtime; they do not make Aurora a privacy proxy or anonymity system.

Users should never publish Roblox cookies, passwords, authentication tickets,
or private support logs.

## 9. Children and Roblox accounts

Aurora does not operate a separate social network or account system for
children. Age-related account controls, parental controls, advertising rules,
and child privacy for Roblox services remain governed by Roblox and applicable
law. A parent or guardian should review Roblox's current policies when a child
uses Roblox through Aurora.

## 10. Changes to this policy

Material privacy changes must update this document's date and be visible in
repository history. Aurora-owned telemetry must not be enabled by default as a
silent policy change.

## 11. Contact

For Aurora privacy questions, use the project's published repository/support
channels. Never include account credentials or authentication secrets in a
public report.
