# [NGC] Group-History-Sync (v2) [PoC] [Draft]

Simple group history sync that uses `peer public key` + `message_id` + `timestamp` (`ppk+mid+ts`) to, mostly, uniquely identify messages and deliver them.

## Requirements

TODO

### File transfers

For sending packs of messages. A single message can be larger than a single custom packet, so this is a must-have.

## Procedure

Peer A can request `ppk+mid+ts` list for a given time range from peer B.

Peer B then sends a filetransfer (with special file type) of list of `ppk+mid+ts`.
Optionally compressed. (Delta-coding / zstd)

Peer A keeps doing that until the desired time span is covered.

After that or simultaniously, Peer A requests messages from peer B, either indivitually, or packed? in ranges?.
Optionally compressed.

During all that, peer B usually does the same thing to peer A.

## Traffic savings

It is recomended to remember if a range has been requested and answered from a given peer, to reduce traffic.

While compression is optional, it is recommended.

## Message uniqueness

This protocol relies on the randomness of `message_id` and the clocks to be more or less synchronized.

However, `message_id` can be manipulated freely by any peer, this can make messages appear as duplicates.

This can be used here, if you don't wish your messages to be syncronized (to an extent).

## Security

Only sync publicly sent/recieved messages.

Only allow sync or extended time ranges from peers you trust (enough).

The default shall be to not offer any messages.

Indirect messages shall be low in credibility, while direct synced (by author), with mid credibility.

Either only high or mid credibility shall be sent.


Manual exceptions to all can be made at the users discretion, eg for other self owned devices.

## File transfer requests

TODO: is reusing the ft request api a good idea for this?

| fttype     | name | content (ft id) |
|------------|------|---------------------|
| 0x00000f00 | time range | - ts start </br> - ts end </br> - supported compression? |
|            | TODO: id range based request? | |
| 0x00000f01 | single message | - ppk </br> - mid </br> - ts |

## File transfers

| fttype     | name | content |
|------------|------|---------------------|
| 0x00000f00 | time range | - feature bitset (1byte? different compressions?) </br> - ts start </br> - ts end </br> - list size </br> \\+ entry `ppk` </br> \\+ entry `mid` </br> \\+ entry `ts` |
| 0x00000f01 | single message | - message type (text/textaction/file) </br> - text if text or action, file type and file id if file |

## TODO

- [ ] figure out a pro-active approach (instead of waiting for a range request)
- [ ] compression in the ft layer? (would make it reusable) hint/autodetect?

