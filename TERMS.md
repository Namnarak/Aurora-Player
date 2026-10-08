# Aurora Terms of Use

**Effective date:** 8 September 2026 · **Last reviewed:** 8 September 2026

These Terms describe the supported use of Aurora distributions, project-run
services, and project support. They do **not** replace or narrow rights granted
by the open-source license text in `LICENSE`, applicable per-file SPDX notices,
or third-party licenses.

## 1. What Aurora is

Aurora is an independent compatibility project that runs supported Roblox
clients on Linux and related environments. Aurora is not affiliated with,
endorsed by, sponsored by, or operated by Roblox Corporation or VinegarHQ.
Roblox and related names, marks, services, and client software belong to their
respective owners.

Aurora does not distribute Roblox account credentials and does not intend to
distribute modified Roblox client code as part of the normal Player payload.
The supported Player update path verifies the Roblox package identity and
signing trust before a payload can become active.

## 2. Roblox terms still apply

Using Roblox through Aurora does not replace the agreement between the user and
Roblox. Users remain responsible for complying with the current Roblox Terms of
Use, Community Standards, and other rules that apply to their Roblox account
and activity.

Roblox may change its services, security requirements, client behavior,
protocols, or compatibility at any time. Aurora does not promise continued
access to Roblox.

Official Roblox legal documents are available from Roblox Support:

- https://en.help.roblox.com/hc/en-us/articles/115004647846-Roblox-Terms-of-Use
- https://en.help.roblox.com/hc/en-us/categories/45348186499220-Legal-Documents

## 3. Supported-use safety policy

Aurora is maintained as a compatibility runtime, not an exploit platform.
Project-provided builds, infrastructure, and support are not intended for:

- cheating, exploit execution, unauthorized automation, or account abuse;
- bypassing Roblox anti-cheat, attestation, authentication, moderation, or
  other security controls;
- injecting untrusted code into the Roblox client;
- repackaging Roblox with hidden executors, explorers, loaders, or similar
  unauthorized components;
- unauthorized access to another person's account, device, data, or service;
- concealing Aurora in order to evade a Roblox security or enforcement system.

The project may decline support for configurations or modifications that fall
outside this supported-use policy. This project policy does not alter rights
that an applicable open-source license independently grants to source code.

## 4. Verified production path

Normal Aurora Player releases are expected to use the verified managed-payload
path. Aurora may reject, quarantine, or refuse to activate a payload when its
package identity, signing certificate, hashes, ABI compatibility, approval
state, or integrity checks do not meet the production policy.

Development and compatibility experiments must remain explicitly separated
from normal production launches. A development override is not a statement
that Roblox has approved that configuration.

## 5. Accounts and credentials

Users are responsible for the security of their Roblox accounts. Aurora must
not ask users to publish `.ROBLOSECURITY` values, authentication tickets,
passwords, or other secrets in bug reports or public support channels.

Aurora Player is designed to keep its own Roblox credential state separate from
other Roblox compatibility clients. Importing credentials from unrelated
applications without the user's explicit action is outside the supported
privacy model.

## 6. Third-party services

Aurora can interact with services operated by third parties, including Roblox,
Roblox package-distribution sources, operating-system package repositories,
and optional integrations such as Discord. Those services have their own terms
and privacy policies. Aurora cannot control their availability or data
practices.

Aurora Studio is a separate optional launcher. Roblox Studio, Wine, and any
third-party Studio tooling remain subject to their own licenses and terms.

## 7. No warranty

Aurora is experimental compatibility software. To the maximum extent permitted
by applicable law and by the licenses covering the software, it is provided
without warranties of availability, compatibility, fitness for a particular
purpose, account safety, uninterrupted operation, or continued Roblox access.

Users should keep backups of important data and should not rely on Aurora as
the sole copy of account, project, or Studio data.

## 8. Security response

The project may temporarily disable an update path, release, integration, or
feature when doing so is reasonably necessary to protect users, preserve
payload integrity, respond to a vulnerability, or avoid distributing a known
unsafe configuration.

Security fixes should favor transparent compatibility and containment over
concealment or bypass of Roblox security systems.

## 9. Changes to these Terms

Material changes should update the effective/review date and be visible in the
repository history. A policy change must not silently override an applicable
open-source license.

## 10. Contact

For project questions, use the Aurora repository or its published support
channels. Do not include passwords, Roblox cookies, authentication tickets, or
other secrets in public reports.
