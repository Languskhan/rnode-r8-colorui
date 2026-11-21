# ColorUI notifications

Graphic pop-ups for things that matter, never for every packet. RX/TX traffic
is already live on the pages (TX light, rings, Now Heard, Channel waterfall).

| Event | Style | Shown | Rule |
|---|---|---|---|
| New area (city locator) | card: "Welcome to <city>" sign scene | 4.5 s | always; jumps the queue; chirp |
| Message for you | card: mailbox + pigeon + chirps | 3.5 s | always; repeats within 15 s fold into "3 MESSAGES"; 3-note chirp |
| New node (first announce) | toast: waving guy | 2.2 s | bursts within 3 s fold into "N NEW NODES" |
| Host linked / host lost | toast: laptop + RNode | 2-2.5 s | dropped if repeated within 10 s |
| GPS fix / GPS lost | toast: satellite | 2 s | dropped if repeated within 60 s |
| Interference | toast: lightning | 2 s | dropped if repeated within 60 s |
| Battery low (<=15%) | toast: battery | 3 s | once per crossing; re-arms above 20% |

Queue holds 4. Cards cut a toast short and go first. Privacy mode hides names.

## Message for you

The RNode can't read messages (Reticulum encrypts them end to end). It can
see the destination hash on the outside of each packet. Tell it your own
address and it pops the card when a DATA packet or link request for that
address arrives:

```
[[ColorUI RNode]]
  type = ColorUIRNodeInterface
  ...
  colorui_my_destinations = <your LXMF address from Sideband or NomadNet>
```

Messages fetched later from a propagation node arrive inside a link to that
node, so they can't be spotted this way. A host app can raise any pop-up
itself with host frame type 4 (EVENT), e.g. "from Alice".
