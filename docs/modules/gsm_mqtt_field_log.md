# GSM / MQTT field checklist log

Operator worksheet for [`gsm_modem.md`](gsm_modem.md) § «Сценарии проверки» **4–7**.
Firmware cannot substitute this run — fill on device after flash.

| # | Scenario | Pass? | Date | Notes / log snippets |
|---|----------|-------|------|----------------------|
| 4 | Leave READY / reattach handoff | ☐ | | Expect: `[Sim800Tcp] stop` / CIPCLOSE before SAPBR; `[Cellular] GSM left READY — draining MQTT`; backoff grows on repeat fails; clears after stable Connected |
| 5 | `last_err` delivery | ☐ | | Expect: status carries `last_err` after disconnect; mid-SEND TCP drop → `last_err` again after reconnect (not eaten by early markDelivered) |
| 6 | cmd ack gate | ☐ | | Ctrl busy: no execute without 202 (`cmd not queued`); live session: `run` → 202 then 200/500 |
| 7 | Baud search ceiling | ☐ | | Wrong baud / dead modem: after `BAUD_SEARCH_MAX_PASSES` → ERROR + `GSM_NO_RESPONSE`, not infinite search |

Also re-check smoke **1–3** (HTTP RX, MQTT handshake + `pub avail online` only after SUBACK, SoftAP UI + SSE `gsmState`/`csq`/`mqttConnected`) when validating a release candidate.
