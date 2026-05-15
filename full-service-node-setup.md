# Full Service Node Setup

> Note: This page provides a basic Service Node setup reference from the wallet documentation side.  
> For the latest and more detailed Proof-of-Stake, Service Node staking, restart, recovery, security, and status documentation, please refer to the [Judecoin Proof of Stake Evolution](https://github.com/Judecoin/pos-evolution) repository.

#### Running a Judecoin Service Node: Requirements

These are the current basic requirements for running a Service Node.

| Spec                                  | Requirement                                                  |
| ------------------------------------- | ------------------------------------------------------------ |
| Latest Judecoin Service Node software | Latest Service Node binaries                                 |
| Server operating system               | Ubuntu 20.04+ (latest LTS recommended) or Debian 11+ (latest stable recommended) |
| Storage                               | 40GB or more                                                 |
| RAM                                   | 4-8GB (4GB absolute minimum)                                 |
| Connectivity                          | 100Mb or faster                                              |
| Traffic                               | 1TB per month or more                                        |
| Power                                 | Redundant with remote cycling ability, as found in most data centres |

We assume you already have a remote server and that it has been configured and is ready for use.

#### Step 1: Server Firewall Configuration

If you are using a firewall, ensure that the following ports are open/reachable:

- Port 16060 (blockchain syncing)
- Port 16063 (Service Node to Service Node)

#### Step 2: Judecoin Service Node Installation and Operation

#### 2.1 Install Judecoin released binaries

```
wget https://www.judecoin.io/storage/files/cli/judecoin-linux-x64-v3.1.2.tar.bz2

tar -jxvf judecoin-linux-x64-v3.1.2.tar.bz2

cd judecoin-x86_64-linux-gnu-v3.1.2
```

##### Check version information

```
./judecoind --version
```

#### 2.2 Run `judecoind` with Service Node mode

There are two recommended ways to run the Judecoin node:

1. Directly run it as a daemon process via command-line parameters (Step 2.3).
2. Run it as a system service (Step 2.4).

#### 2.3 Run as daemon via console

```bash
./judecoind --service-node --service-node-public-ip <YOUR_SERVER_PUBLIC_IP> --no-zmq --detach
```

Note: Replace `<YOUR_SERVER_PUBLIC_IP>` with your server public IP, for example: `1xx.2xx.3xx.4xx`.

#### 2.4 Start `judecoind` as a systemd service

#### 2.4.1 Create `judecoin-node.service`

`judecoin-node.service` example:

```
#/etc/systemd/system/judecoin-node.service
[Unit]
Description=Judecoin network node
ConditionPathExists=/usr/local/bin/judecoind
After=network-online.target

[Service]
Type=simple
User=judecoin
WorkingDirectory=/usr/local/bin
ExecStart=/usr/local/bin/judecoind --non-interactive --config-file /your/path/judecoind.conf
Restart=on-failure
RestartSec=10s
LimitNOFILE=50000

[Install]
WantedBy=multi-user.target
```

`judecoind.conf` example:

```
#judecoind.conf

# Configuration for judecoind
# Syntax: any command line option may be specified as 'clioptionname=value'.
#         Boolean options such as 'no-igd' are specified as 'no-igd=1'.
# See 'judecoind --help' for all available options.

data-dir=/var/lib/judecoin
log-file=/var/log/judecoin/judecoin.log
log-level=0
no-zmq=1
service-node=1
service-node-public-ip=YOUR_SERVER_PUBLIC_IP
```

#### 2.4.2 Start `judecoin-node.service`

```bash
sudo cp your/path/judecoin-node.service /etc/systemd/system/judecoin-node.service
sudo systemctl daemon-reload
sudo systemctl enable judecoin-node.service
sudo systemctl start judecoin-node
```

#### 2.5 Interacting with the running `judecoind`

If you run the `judecoind` command with an appended `judecoind` command, the command forwards this instruction to the running `judecoind`.

For example, to get the current `judecoind` status, run:

```
./judecoind status

./judecoind print_sn_status
```

For a full list of supported commands, run:

```
./judecoind --help
```

To see the output log of your node, run:

```
tail -f -n 100 /var/log/judecoin/judecoin.log
```

This is useful to see if your node is syncing with the blockchain and to see other diagnostic messages that may come up from time to time. Press `Ctrl-C` to stop watching the log.

#### Step 3: Service Node Registration

This section of the guide is split into two parts:

- If you are an individual staker and do not require any other contributors to run your Service Node, please refer to Step 3.1.
- If you want to run a pooled Service Node, please refer to Step 3.2.

#### 3.0.1: Retrieving your wallet address

You'll need your wallet address to register your Service Node. Run the `address` command from within the Judecoin CLI wallet, and copy the output.

> Note: Do not use subaddresses for staking. Subaddresses are currently unsupported for staking in the Judecoin wallet.

#### 3.1: Individual staking

To run a Service Node as the sole contributor, you'll need:

- A fully synchronized, up-to-date Judecoin daemon running on your Service Node.
- A Judecoin wallet with at least 23600 JUDE in it, to meet the staking requirement to register your Service Node.

#### 3.1.1: Preparing your node for registration

Log in, if not already logged in, to the VPS running the Service Node, then run the following command:

```
./judecoind prepare_registration
```

The daemon will output the current staking requirement and prompt you with an input to clarify whether you are an individual staker or you will be running a pool. Type `y` and press Enter, as you will be the sole staker.

The daemon will now prompt you for the operator's Judecoin address. This is the address saved in Step 3. Retrieve this address, copy it, then paste it into the terminal and press Enter.

The daemon will now ask for a final confirmation. If you agree with the information provided, type `y` and press Enter.

The daemon will output a command which looks similar to:

```
register_service_node 10949992 JBAo6emU36qSJU12Mdp6Pc1ioAFyRUiHbDDqbuoPRgSn6z6tnnsUzEHgRZ4axJb73Neb7DJjnfEHrQdjgUTvD7ZhF8hZnDj 23600000000000 16 8dc0d93a5f9ce759448e38bf9bbbd1f439a09d0c853e0b91626e50f6f07390f3 b4b251aa3927f0fa046dc404e2f8e86d907de75255426140cab047e013a0dc021425c532a0bf46885f72ea9fe576d0b68d53f55639dc217d618e89b9dd6b3208
```

> *NOTE: You must run the command which* ***your*** *daemon outputs, and* ***not*** *the command shown above.*

#### 3.1.2: Registering your Service Node

To stake and register your Service Node, open your Judecoin CLI wallet with a balance of at least 23600 $JUDE. Simply paste the `register_service_node` command from Step 3.1.1 above into the CLI wallet prompt and press Enter.

Well done! Continue to `Step 4: Service Node check` to make sure your Service Node is running properly.

#### 3.2: Setting up a pooled Service Node

#### *Minimum contribution rules*

The Service Node staking requirement is fixed at 23600 $JUDE. Service Nodes accept at most 9 contributions, meaning the minimum contribution to a Service Node is `<Remaining Staking Requirement> ➗ <Number of Remaining Contributors>`.

When setting up reserved spots in a pooled Service Node, the node administrator must ensure the reserved stake amounts each meet the minimum staking requirement; contributors then simply stake their reserved amounts.

#### 3.2.1: Pool operator

The operator is the individual who will be hosting the pool and running the server hosting the Service Node, thus incurring the operating expenses involved in running a node.

To be an operator, you'll need to have:

- A server running a fully synchronized, up-to-date `judecoind`.
- A Judecoin wallet with at least 5900 JUDE, to meet the minimum Service Node operator staking requirement.
- 1-3 other contributors who also have a Judecoin wallet, either the CLI or GUI wallet, with enough JUDE to meet their portion of the total stake.
- If the operator wants to reserve contribution spots for specific contributors, the operator will need the addresses of the contributors and the amounts the 1-3 contributors will stake.

If you have the above ready, you can now prepare the Service Node.

Log in, if not already logged in, to the VPS running the Service Node, then run the following command:

```
./judecoind prepare_registration
```

`judecoind` will prompt you to specify if you will contribute the entire stake. Because you're running a pooled Service Node, type `n` and press Enter.

Next, `judecoind` will request input for your desired operator fee. This value, which can be between 0-100, represents the percentage of the reward the operator will receive **before** the reward is distributed to all shareholders. For example, if you want to set up a 10% operator cut, you would type `10` and press Enter.

> For example, imagine a Service Node with 4 contributors, including the operator, all staking equal amounts of 25%. If the operator specified a 10% fee at this step, they would automatically receive 10% of the Service Node rewards, and the remaining 90% would then be split equally between the operator and the other 3 contributors.

The terminal will now display the minimum reserve the operator can contribute, and request input for the amount, in JUDE, you as the operator wish to contribute. Type your desired `<operator contribution>` and press Enter.

Once you've set your desired stake amount, you'll be prompted to either reserve spots for individuals that have already agreed to stake into the Service Node, or leave the pool open for anyone to contribute.

#### Option one: Reserved pool

If you want to reserve spots for specific contributors, type `y` at this prompt and press Enter.

The terminal will now prompt you for the number of additional contributors you've organized to stake into this Service Node. Type in the number of reserved contributors, **not including yourself**, and press Enter.

The daemon will now prompt you for the operator's Judecoin address. This is the address saved in Step 3. Retrieve this address, copy it, then paste it into the terminal and press Enter.

Next, you need to input the amount of JUDE each contributor will stake, and the Judecoin wallet address of the specific contributor(s).

> *NOTE: It is possible to reserve only some of the required total stake for specific contributors, while leaving the remaining staking amount open for other contributors.*

The daemon will display a summary of the information you've entered. This is your chance for a final check to make sure the correct information has been entered. To confirm the information is correct, type `y` and press Enter.

#### Option two: Open pool

If the operator wishes to leave the pool open to contributions from others, they should type `n` at the reservation prompt and press Enter. The terminal will prompt the operator to input their address. Once the address has been entered, the terminal will display the remaining portion that needs to be contributed by others. If you agree, type `y` and press Enter.

The daemon will display a summary of the information you've entered. This is your chance for a final check to make sure the correct information has been entered. To confirm the information is correct, type `y` and press Enter.

#### Step 3.2.2: Registering your shared Service Node

Regardless of which option, closed or open, you've chosen, the daemon will output a command which looks similar to:

```
register_service_node 10949992 JBAo6emU36qSJU12Mdp6Pc1ioAFyRUiHbDDqbuoPRgSn6z6tnnsUzEHgRZ4axJb73Neb7DJjnfEHrQdjgUTvD7ZhF8hZnDj 5000000000 16 8dc0d93a5f9ce759448e38bf9bbbd1f439a09d0c853e0b91626e50f6f07390f3 b4b251aa3927f0fa046dc404e2f8e86d907de75255426140cab047e013a0dc021425c532a0bf46885f72ea9fe576d0b68d53f55639dc217d618e89b9dd6b3208
```

> *NOTE: You must run the command which* ***your*** *daemon outputs, and* ***not*** *the command shown above.*

Copy the whole line of text from your daemon and paste it into your notepad, as you'll need to run this command from within your Judecoin CLI wallet.

You have 2 weeks from the moment of registering the Service Node to run the `register_service_node` command. However, it is advised to do it as soon as possible.

Before you disconnect from your VPS, run the following command:

```
./judecoind print_sn_key
```

This will output information about your Service Node, including the long string of random letters and numbers after the characters `SN:`. This string is your Service Node's public key, used to identify your Service Node on the list of registered and operational Service Nodes. Select and copy the public key. Do not copy any of the surrounding information.

On your local machine, open your Judecoin CLI wallet and make sure your wallet contains at least 5900 JUDE to meet the Service Node staking requirement. Once you're in your wallet and have checked the balance, run the command which was provided above when you ran the `prepare_registration` command. The wallet will prompt you to confirm your password, then the amount of JUDE to stake. Confirm this by typing `y` and pressing Enter.

Once this command completes, your staking transaction will be sent to be included on the blockchain. It may take a few minutes for the transaction to be mined into a block. You can check the status using the following command:

```
./judecoind print_sn_status
```

You can also check your node's status by looking for your `<Service Node Public Key>` in the "Service Nodes Awaiting Contributions" section on [the Judecoin block explorer](https://www.judeblock.org/).

Once the Service Node registration is received, you can send the `<Service Node Public Key>` to your contributors, along with the amount of $JUDE they are required to stake.

At this point, you'll need to wait until all contributors have staked before the Service Node activates and becomes eligible to begin receiving rewards.

#### Staking to a shared node as a contributor

A guide on staking to a shared Judecoin Service Node as a contributor is coming soon.

#### Step 4: Service Node Status Check

After you've staked to your Service Node, or after all contributors have staked if you're running a shared node, you'll need to check if your Service Node's public key is on the list of Service Nodes which are operational on the network. This will prove that your Service Node is running, recognised, and eligible to receive rewards.

Connect to the VPS where the Service Node is running and run the following command to retrieve your Service Node's public key:

```
./judecoind print_sn_key
```

This will output a long string of letters and numbers: your Service Node's public key. This public key is used to identify your Service Node on the list of registered and operational Service Nodes. Select and copy the public key.

You can now jump onto [Judecoin block explorer](https://www.judeblock.org/), open the full list of active Service Nodes, and use `Cmd+F`/`Ctrl+F` to check if your Service Node's public key appears in the list.

#### Keeping your binaries up to date

### Backups

You should immediately make a backup of your Service Node's secret key. This will allow you to recover from a disaster or to migrate your node to a different network service provider, should that prove necessary in the future.

The command to reveal the secret key is:

```
./judecoin-sn-keys show /your/path/judecoind/data-dir/key_ed25519
```

And the command to restore it is:

```
./judecoin-sn-keys restore /your/path/judecoind/data-dir/key_ed25519
```

### Conclusion

Well done! Your Service Node is configured, operational, and will now begin receiving rewards.