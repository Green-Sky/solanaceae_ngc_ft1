# [NGC] Group-History-Sync (v2.1) [PoC] [Draft]

Simple group history sync that uses `timestamp` + `peer public key` + `message_id` (`ts+ppk+mid`) to, mostly, uniquely identify messages and deliver them.

Messages are bundled up in a `msgpack` `array` and sent as a file transfer.

## Requirements

TODO: more?

### Msgpack

For serializing the messages.

### File transfers

For sending packs of messages.
Even a single message can be larger than a single custom packet, so this is a must-have.
This also allows for compression down the road.

## Procedure

Peer A can request `ts+ppk+mid+msg` list for a given time range from peer B.

Peer B then sends a filetransfer (with special file type) of list of `ts+ppk+mid+msg`.
Optionally compressed. (Delta-coding? / zstd)

Peer A keeps doing that until the desired time span is covered.

During all that, peer B usually does the same thing to peer A.

TODO: deny request explicitly. also why (like perms and time range too large etc)

## Traffic savings

It is recomended to remember if a range has been requested and answered from a given peer, to reduce traffic.

While compression is optional, it is recommended.
Timestamps fit delta coding.
Peer keys fit dicts.
Message ids are mostly high entropy.
The Message itself is text, so dict/huffman fits well.

TODO: store the 4 coloms SoA instead of AoS ?

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
| 0x00000f02 | time range msgpack | - ts start </br> - ts end |

## File transfer content

| fttype     | name | content                    | note |
|------------|------|----------------------------|---|
| 0x00000f02 | time range msgpack | `message list` in msgpack | |

### time range msgpack

Msgpack array of messages.

```
 name                    | type/size         | note
-------------------------|-------------------|-----
- array                  | 32bit number msgs
  - ts                   | 64bit deciseconds
  - ppk                  | 32bytes
  - mid                  | 16bit
  - msgtype              | enum (string or number?)
  - if text/action       |
    - text               | string            | maybe byte array instead?
  - if file              |
    - fkind              | 32bit enum        | is this right?
    - fid                | bytes kind        | length depends on kind
```

Name is the actual string key.
Data type sizes are suggestions, if not defined by the tox protocol.

How unknown `msgtype`s are handled is client defined.
They can be fully ignored or displayed as broken.

## TODO

- [ ] figure out a pro-active approach (instead of waiting for a range request)
- [ ] compression in the ft layer? (would make it reusable) hint/autodetect/autoenable for >1k ?

