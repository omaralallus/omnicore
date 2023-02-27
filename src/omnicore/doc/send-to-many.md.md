# Send-to-Many transactions
 
A new transaction structure allows to include multiple transfers in one transaction.

The payload of this new transaction type includes one token identifier, which defines the tokens to send. It also includes a list of receiver -> amount mappings to specify, which receiver receives how many tokens. Receivers are specified by actual Bitcoin transaction outputs, which are referenced in the payload. One or more receivers can be defined.

The new transaction type has `7` as identifier.

The payload structure may look like this:

```
[transaction version] [transaction type] [token identifier to send] [number of outputs following] [receiver output #] [amount to send] ( [output # of receiver] [amount to send] ... )
```

## Example: only one receiver

In the following example, only one receiver is defined, basically mirroring the behavior or regular simple sends.

1.0 Omni are sent to the address specified in output 1.

A single transfer command has the following structure:

| **Field**                | **Type**            | **Value**           |
| ------------------------ | ------------------- | ------------------- |
| Transaction version      | Transaction version | 0                   |
| Transaction type         | Transaction type    | 7 (= Send-to-Many)  |
| Token identifier to send | Token identifier    | 1 (= Omni)          |
| Number of outputs        | Integer-one byte    | 1                   |
| Receiver output #        | Integer-one byte    | 1 (= vout 1)        |
| Amount to send           | Number of tokens    | 1 0000 0000 (= 1.0) |

Actual payload:

```
0000 0007 00000001 01 01 0000000005f5e100
```

## Example: three receivers

Let's imagine a transaction with one input and six outputs. One output for the payload and three outputs for token receivers. Bitcoin values are omitted in this example, but we simply assume the amount of incoming coins is enough to cover the whole transaction. There is another output, which is not relevant for us, and one for change.

![visualized](https://i.imgur.com/ok6dD91.png)

The first input has tokens associated with it:

| **Input Index** | **Token identifier**         | **Token amount**          |
| ----------------| ---------------------------- | ------------------------- |
| 0               | 31 (USDT)                    |  100 0000 0000 (=  100.0) |

The outputs of the transaction are as follows:

| **Output Index** | **Script type**                   |
| -----------------| --------------------------------- |
| 0                | Payload with commands             |
| 1                | Pay-to-pubkey-hash (recipient 1)  |
| 2                | Pay-to-pubkey-hash (recipient 2)  |
| 3                | Pay-to-pubkey-hash (not relevant) |
| 4                | Pay-to-script-hash (recipient 3)  |
| 5                | Pay-to-pubkey-hash (our change)   |

The first output of the transaction contains the payload with the following data:

| **Field**                | **Type**            | **Value**             |
| ------------------------ | ------------------- | --------------------- |
| Transaction version      | Transaction version | 0                     |
| Transaction type         | Transaction type    | 7 (= Send-to-Many)    |
| Token identifier to send | Token identifier    | 31 (= USDT )          |
| Number of outputs        | Integer-one byte    | 3                     |
| Receiver output #        | Integer-one byte    | 1 (= vout 1)          |
| Amount to send           | Number of tokens    | 20 0000 0000 (= 20.0) |
| Receiver output #        | Integer-one byte    | 2 (= vout 2)          |
| Amount to send           | Number of tokens    | 15 0000 0000 (= 15.0) |
| Receiver output #        | Integer-one byte    | 4 (= vout 4)          |
| Amount to send           | Number of tokens    | 30 0000 0000 (= 30.0) |

Actual payload:

```
0000 0007 0000001f 03 01 0000000077359400 02 0000000059682f00 04 00000000b2d05e00
```

20.0 USDT are transferred to the transaction output with index 1, 15.0 USDT to the output with index 2 and 30.0 USDT are transferred to the output with index 4. Given that the sender had a balance of 100.0 USDT, there is a leftover of 35.0 USDT, which were not moved and still belong to the sender.

## Notes and remarks

- The transaction fails, if there are not enough tokens for all transfers.
- The transaction fails, if any output references an invalid destination or a destination, which isn't considered as valid destination in terms of the Omni Layer protocol.
- It does not chance anything about Omni Layer's balanced based approach.
- The order of output mappings is not strictly in order. You may first define a send to output #3, then to output #0.
- When constructing the transaction, be careful about the placement of change addresses. If it is inserted randomly, it may affect the mapping.
- Other Omni Layer rules apply, in particular: only the first transaction input specified how many tokens are there to send.
- This is not "send-from-many".
- It is possible to construct a valid transaction with no receiver.
