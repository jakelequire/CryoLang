# TLS test fixtures

Throwaway self-signed certificate and key used only by
`tests/tests/stdlib/net_tls.cryo` for a loopback handshake. CN is
`localhost`; the client verifies nothing (`danger_accept_invalid_certs`),
so this is never a trust anchor anywhere.

Regenerate with:

```
openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem \
    -days 36500 -nodes -subj "/CN=localhost"
```
