# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| 2.0.x   | ✅ Yes             |
| < 2.0   | ❌ No (legacy Python) |

## Reporting a Vulnerability

If you discover a security vulnerability, please report it responsibly:

1. **Do NOT** open a public issue
2. Email: ph4ntom-rev@users.noreply.github.com
3. Or use [GitHub Security Advisories](https://github.com/ph4ntom-rev/antigravity-chrome-bridge/security/advisories/new)

## Security Considerations

### By Design
- The bridge executes arbitrary JavaScript in browser tabs via `chrome_evaluate_js`
- The extension has `<all_urls>` host permissions for full automation capability
- The HTTP bridge server (`127.0.0.1:13371`) has no authentication (localhost-only)

### Mitigations
- All network listeners bind to `127.0.0.1` only (not `0.0.0.0`)
- CDP connections are plaintext but localhost-only
- Extension uses Manifest V3 service worker model
- No secrets or credentials are stored or transmitted

## Response Timeline

- **Acknowledgment**: Within 48 hours
- **Assessment**: Within 7 days
- **Fix**: Depends on severity, typically within 30 days
