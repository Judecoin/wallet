# CLI Wallet setup (Linux-Ubuntu)
This guide covers installing and configuring the Jude CLI (Command Line Interface) Wallet on Linux.

If you are new to console or terminal commands and/or would prefer a wallet with buttons and JUDE branding, we suggest you download the [GUI Wallet](https://www.judecoin.io/downloads).

Before following the guide below, you'll need to download the CLI Wallet for Linux. 

# Step 1: Opening judecoin-wallet-cli and judecoind.

To use the judecoin-wallet-cli we must first have the daemon, judecoind, up and running. The judecoind is your node which the judecoin-wallet-cli broadcasts through. Without the node running the judecoin-wallet-cli will not be able to operate.

Open up the judecoind file in the folder you extracted the release from. 

Let the daemon run until the node is completely synced, you will know the node is synced once the terminal outputs the following text:

```
**********************************************************************

You are now synchronized with the network.

Use the "help" command to see the list of available commands.

**********************************************************************
```

Now the daemon is synced we can run the judecoin-wallet-cli file.


# Step 2: Setting up your judecoin-wallet-cli account.

If this is your first time opening the judecoin-wallet-cli it will request for you to specify a wallet name. For the purposes of this user guide we will use the example name MyWallet

```
Specify wallet file name (e.g., MyWallet). If the wallet doesn't exist, it will be created.

Wallet file name (or Ctrl-C to quit): MyWallet
```

Because this is the first time we have used the name MyWallet the following text will appear in our terminal. Type in Y or Yes to confirm your wallet name.

```
No wallet found with that name. Confirm creation of new wallet named: MyWallet

(Y/Yes/N/No): Yes
```
The judecoin-wallet-cli has now generated us a wallet called MyWallet and is now prompting us for a password for our generated wallet.

#### Please note:
- When typing the password, the characters will not appear. It will seem as if you are typing and no text is appearing however the terminal is logging every character your clicking including if it is capitalised or lowercase.
- Write down your wallet name and password on a piece of paper as this information will be required every time we want to enter our wallet.
- Use a password with uppercase letters, lowercase letters, numbers, symbols and make the password at least 9 characters long.

```
Generating new wallet...

Enter a new password for the wallet:

Confirm password:
```
Now once we have chosen our password for the wallet we must choose our language. For the purposes of this user guide I suggest you use English by typing 1 and clicking enter.

```
List of available languages for your wallet's seed:
If your display freezes, exit blind with ^C, then run again with --use-english-language-names
0 : Deutsch
1 : English
2 : Español
3 : Français
4 : Italiano
5 : Nederlands
6 : Português
7 : русский язык
8 : 日本語
9 : 简体中文 (中国)
10 : Esperanto
11 : Lojban
Enter the number corresponding to the language of your choice: 1
```

The judecoin-wallet-cli will generate and spit out several lines of text. Some of the information that was outputted will only ever show once, therefore it is very important to do this next section properly otherwise we may lose access to our account, thus losing access to our funds.

Let’s take a close look at each section of the newly generated wallet:

The text after Generated new wallet shows your public address. This address can be shared and will be used to receive JUDE to your wallet. All Jude public addresses start with an J.... and are followed with a string of characters. The public address shown will be your primary address however multiple public addresses can be generated from this primary address.

You do not need to write down the public address, the command address will re-display it whenever required.

```
Generated new wallet: JAvaQXtDjMhJ87vRQ2ihLE26Xs8zDX1uC43p9ggceYJyEfa1rQ8ZQySBi6p1H5jZ5DbuzZvZHgDyaDjaiW4sJ96vLAM4MfQ
```

The View key address is not to be shared unless you want to show the transactions received to the public address connected to this wallet. You do not need to write down the view key as it can be re-displayed with the command viewkey.

```
View key: fd370cd118cf846df807c873016b9c24d9c75fdae44c64c92c253c3e03041206
```

The next few lines of text show how to navigate the judecoin-wallet-client.
```
**********************************************************************
Your wallet has been generated!
To start synchronizing with the daemon, use the "refresh" command.
Use the "help" command to see a simplified list of available commands.
Use "help all" command to see the list of all available commands.
Use "help <command>" to see a command's documentation.
Always use the "exit" command when closing judecoin-wallet-cli to save 
your current session's state. Otherwise, you might need to synchronize 
your wallet again (your wallet keys are NOT at risk in any case).
**********************************************************************
```

The next section with the random 25 words is your mnemonic seed. The seed is used to easily back-up and restore your wallet without needing any other information. At this stage, grab a pen and paper and write down your 25 words in order (having these words out of order will not restore your wallet) and store the piece of paper in a safe and secure place. If your words are stored in a text file on your computer or stored online, you increase your risk of someone else getting control of your account.

```
**********************************************************************
NOTE: the following 25 words can be used to recover access to your wallet. Write them down and store them somewhere safe and secure. Please do not store them in your email or on file storage services outside of your immediate control.

mechanic playful surfer phone august elope foiled tunnel
afraid rewind cousin kisses sniff orders simplest boil
incur sifting nocturnal bluntly hive candy arsenic below elope
**********************************************************************
```

The last of the outputs are the account balance, because your wallet does not have any JUDE in it currently the balance is showing 0.

Once we receive a transaction of JUDE into our wallet the balance will appear as soon as the transaction is confirmed in one block (usually less than 2 minutes). Once the transaction has been confirmed over 10 blocks the balance will show in unlocked balance.

The unlocked balance is the JUDE available to be spent/sent to other addresses.

```
Starting refresh...

Refresh done, blocks received: 0

Untagged accounts:

Account Balance Unlocked balance Label

* 0 LAXk6e 0.000000000 0.000000000 Primary account

----------------------------------------------------------------------------------

Total 0.000000000 0.000000000

Currently selected account: [0] Primary account

Tag: (No tag assigned)

Balance: 0.000000000, unlocked balance: 0.000000000

Background refresh thread started”
```


