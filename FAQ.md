# Aurora Player FAQ

## How do I enable Discord Rich Presence?

Discord Rich Presence is disabled by default. Open Aurora Player's Settings,
enable Discord activity sharing, and restart the game client if it is already
running. The same setting is stored in `~/.config/aurora/config.yaml` under
`integrations.discord_rpc.enabled`.

## Where can I find logs for a bug report?

Aurora Player stores logs locally at
`~/.local/state/aurora/logs/latest.log`. A custom `XDG_STATE_HOME` changes the
base directory. Attach the log only after checking it for account details or
other private data.

## Can I run multiple Roblox instances?

Aurora Player does not support launching multiple Roblox clients at once. See
the [Terms of Use](TERMS.md) for the project's supported-use policy.

## Does Aurora Player support VR?

No. Aurora Player does not currently include a VR runtime or VR launch mode.
