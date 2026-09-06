# Security policy

Report vulnerabilities using [private GitHub advisories](https://github.com/ph4ntom-rev/antigravity-chrome-bridge/security/advisories/new). Do not include browser cookies, pairing tokens or private page contents.

## Boundaries

The stdio client is trusted and can execute JavaScript, access cookies, navigate pages and modify browser state. This is not a sandbox for untrusted prompts or web content. The extension has broad host permissions. Use a separate browser profile for automation where possible.

The extension HTTP service is disabled unless a token file is configured. When enabled, it binds only to IPv4 loopback, validates Host, rejects unapproved browser Origins and requires a bearer token. CORS preflight exposes no commands and allows only the configured extension Origin. Pairing files are created with owner-only permissions; the extension uses trusted-context local storage. Local software running as the same account can access these credentials. Browser automation results may contain private data even though authentication and request bodies are not logged.

CDP does not support this pairing token. Connections are restricted to the configured loopback port, including debugger WebSocket URLs received from discovery. Anyone able to access the Chrome debugging port can control that profile; never expose it to a network.

Requests, queues, payloads and waits have bounds, but arbitrary JavaScript may continue running inside Chrome after a timeout. A dispatched extension command with a missing result is reported as uncertain and must not be blindly retried. Batch operations are sequential with no rollback.

## Upgrade and revoke

Use the paired extension protocol introduced by this change; older unauthenticated extension servers do not provide these protections. Stop the server before replacing the token file, then update the MCP configuration and extension pairing. Back up no token in a public repository. A compromised browser profile or operating-system account requires remediation outside this bridge.
