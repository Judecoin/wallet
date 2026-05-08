# Linux-Ubuntu CLI Wallet Recovery Guide

## 【Description】

This guide describes how to restore an existing Judecoin CLI wallet on Linux-Ubuntu using the original 25-word seed phrase.

This guide is intended to help Judecoin users recover access to an existing wallet safely through the Linux-Ubuntu CLI wallet recovery process.

## 【Linux-Ubuntu CLI Wallet Recovery Process】

## 【1. Download the CLI Wallet and Start Node Synchronization】

Go to the official Judecoin website to download the CLI wallet:

https://www.judecoin.io

Please make sure to download the wallet only from the official website. Do not use unknown links or third-party sources.

After downloading, extract the compressed wallet package to your Linux server or local Linux machine.

If you are using the CLI package directly on Linux, enter the extracted CLI directory.

Example:

`cd judecoin-x86_64-linux-gnu-v3.1.2`

Start the daemon:

`./judecoind`

The daemon must be running before using `judecoin-wallet-cli`.

`judecoind` is the node that allows the CLI wallet to communicate with the blockchain network. If the daemon is not running, `judecoin-wallet-cli` may not work correctly.

Wait until the node is fully synchronized.

When synchronization is completed, the daemon may show a message similar to:

`You are now synchronized with the network.`

Important:

Do not close the `judecoind` terminal window while using the wallet. You can keep it running in the background or open another terminal window for wallet operations.

## 【2. Open Another Terminal Window】

Open a new terminal window or SSH session.

Enter the same Judecoin CLI directory:

`cd judecoin-x86_64-linux-gnu-v3.1.2`

## 【3. Restore the CLI Wallet】

In the terminal, run the following command:

`./judecoin-wallet-cli --restore-deterministic-wallet --use-english-language-names`

Then press Enter.

## 【4. Enter Wallet Name】

The system will ask you to enter a wallet name.

For example:

`MyWallet`

Then press Enter.

If a wallet file with the same name already exists in the current directory, choose a different wallet name or make sure you are not overwriting an existing wallet file by mistake.

## 【5. Enter the 25-word Seed Phrase】

The system will show:

`Specify Electrum seed:`

Enter your 25-word seed phrase here.

Important notes:

- Add one space between each word;
- Enter all 25 words in the correct order;
- Press Enter after entering all 25 words;
- The terminal usually does not display the seed phrase while you are typing. This is normal;
- Make sure your environment is safe and no one can see your seed phrase;
- Do not take screenshots;
- Do not record the screen;
- Do not send the seed phrase through chat apps or email.

## 【6. Seed Offset Passphrase】

The screen may show:

`Enter seed offset passphrase, empty if none:`

This means: enter the seed offset passphrase if you have one.

Most users do not have this extra passphrase.

If you did not set one before, simply press Enter and leave it empty.

## 【7. Set a New Wallet Password】

The system will ask you to create a new password for the restored wallet.

Enter the new password and press Enter.

Then enter the same password again to confirm.

Important notes:

- The terminal usually does not display the password while you are typing;
- Use a strong password that you can remember;
- Do not store the wallet password together with the seed phrase.

## 【8. Verify the Wallet Address】

After the wallet is restored, the system will show the restored wallet address.

It is recommended to verify whether the address matches your original wallet address.

If the address does not match the expected wallet address, stop and check whether the seed phrase, word order, language option, or seed offset passphrase is correct.

## 【9. Whether to Display the Wallet Seed】

The system may ask whether you want to display the wallet seed.

For safety, it is recommended to enter:

`N`

Then press Enter.

## 【10. Restore from Blockchain Height】

The system may show:

`Restore from specific blockchain height (optional, default 0)`

This means you can choose a specific blockchain height to start wallet scanning from.

If you are not sure, simply press Enter and use the default value `0`.

This may take longer, but it is safer because the wallet will scan from the beginning.

## 【11. Wait for Wallet Refresh and Balance Recovery】

The wallet will start scanning and synchronizing with the blockchain.

If needed, you can manually refresh the wallet by running:

`refresh`

To check wallet balance, run:

`balance`

After synchronization is completed, the wallet will show available balance and locked balance.

## 【12. Exit the Wallet Safely】

When closing the CLI wallet, use:

`exit`

This helps save the current wallet session state properly.

Do not close the terminal directly if the wallet is still refreshing or writing data.

## 【Common Notes】

1. The daemon `judecoind` should be running and synchronized before wallet recovery or wallet refresh.

2. The Linux wallet command does not use `.exe`.

Windows example:

`./judecoin-wallet-cli.exe`

Linux-Ubuntu example:

`./judecoin-wallet-cli`

3. If the wallet restore process takes a long time, it may be scanning from an early blockchain height.

4. If the restored balance does not appear immediately, run:

`refresh`

Then check:

`balance`

5. If the restored address does not match the expected address, check the seed phrase, word order, language setting, and seed offset passphrase.

6. Always download the CLI wallet from the official Judecoin website or official GitHub source.

## 【Security Reminders】

Please pay attention to the following security rules:

- Do not share the 25-word seed phrase with anyone;
- Do not share your wallet password;
- Do not share wallet files;
- Do not share view keys or spend keys;
- Do not take screenshots of your seed phrase;
- Do not store the seed phrase in a normal document on an online computer;
- Do not upload the seed phrase to public cloud storage;
- Do not paste the seed phrase into unknown websites or tools;
- Do not download wallets from unknown links;
- Always download the CLI wallet from the official website or official GitHub source;
- Keep the seed phrase offline;
- Use encrypted storage for important backups;
- Keep at least two offline backups in separate safe locations.

## 【Conclusion】

This guide provides a Linux-Ubuntu CLI wallet recovery process for restoring an existing Judecoin wallet using the original 25-word seed phrase.

Clear wallet recovery procedures can help users recover wallet access safely, reduce operational mistakes, and protect important wallet credentials.
