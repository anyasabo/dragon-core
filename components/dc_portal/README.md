# dc_portal

`dc_portal` is the common Dragon-family management plane. It starts the ESP-IDF
HTTP server, serves the `dc_ui` SPA in both station and AP modes, runs captive DNS,
and exposes JSON endpoints for Wi-Fi scans and credentials, fallback-AP settings,
event logs, OTA, and factory reset.

Products supply identity, their API route table, and optional callbacks:

- `describe_product` returns a schema containing sections and fields for settings
  that are not common to every Dragon device.
- `apply_product` validates and persists submitted product settings. Existing NVS
  namespaces and keys remain the product/component's responsibility.
- `authorize` gates mutations. Wi-Fi provisioning remains open only while the
  device is in captive-portal mode so an unconfigured device is recoverable.
- `guard_operation` keeps product safety policy in force before OTA or reset.
- `validate_image` restricts which ESP-IDF project identities may be selected
  for the next boot after an otherwise valid OTA upload.
- `factory_reset` clears product settings before core clears Wi-Fi credentials.
- `httpd_config` and `register_product_routes` preserve product server tuning and
  allow an existing API implementation to register routes without owning the
  server lifecycle.

The HTTP server has one task and serves one request at a time. `dc_portal`
gives each socket a request deadline (`dc_portal_deadline.h`). A request must
arrive in 20 s from its first byte, with its headers and its body. If it is
slower, the server closes the socket. An OTA upload gets 1 s more for each
1 KB that arrives. The deadline does not stop an idle keep-alive socket or an
event stream. `dc_portal` uses the transport context and the receive and send
functions of each session for this. A product must not set them, but it can
set `open_fn` in its `httpd_config`.

The provisioning contract is versioned separately at
`GET /api/v1/provisioning`. Product API v2 state and command semantics are not
interpreted by core; the supplied route table continues to own them.
