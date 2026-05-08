# Windows CLI Wallet Recovery Guide

## 【Description】

This guide describes how to restore an existing Judecoin CLI wallet on Windows using the original 25-word seed phrase.

This guide is intended to help Judecoin users recover access to an existing wallet safely through the Windows CLI wallet recovery process.

## 【Windows CLI Wallet Recovery Process】

## 【1. Download the CLI Wallet and Start Node Synchronization】

Go to the official Judecoin website to download the CLI wallet:

https://www.judecoin.io

Please make sure to download the wallet only from the official website. Do not use unknown links or third-party sources.

After downloading, extract the compressed wallet package to your local computer.

Open the extracted folder and double-click:

`judecoind`

This will start the daemon and begin blockchain synchronization.

Important:

Do not close the `judecoind` window. You can minimize it and let it continue running.

Then open the extracted folder, right-click in an empty area, and select:

`Open in Terminal`

This will open the command terminal for wallet operations.

## 【2. Restore the CLI Wallet】

In the terminal, run the following command:

`./judecoin-wallet-cli.exe --restore-deterministic-wallet --use-english-language-names`

Then press Enter.

## 【3. Enter Wallet Name】

The system will ask you to enter a wallet name.

For example:

`MyWallet`

Then press Enter.

## 【4. Enter the 25-word Seed Phrase】

The system will show:

`Specify Electrum seed:`

Enter your 25-word seed phrase here.

Important notes:

- Add one space between each word;
- Press Enter after entering all 25 words;
- The terminal usually does not display the seed phrase while you are typing. This is normal;
- Make sure your environment is safe and no one can see your seed phrase;
- Do not take screenshots;
- Do not record the screen;
- Do not send the seed phrase through chat apps or email.

## 【5. Seed Offset Passphrase】

The screen may show:

`Enter seed offset passphrase, empty if none:`

This means: enter the seed offset passphrase if you have one.

Most users do not have this extra passphrase.

If you did not set one before, simply press Enter and leave it empty.

## 【6. Set a New Wallet Password】

The system will ask you to create a new password for the restored wallet.

Enter the new password and press Enter.

Then enter the same password again to confirm.

Important notes:

- The terminal usually does not display the password while you are typing;
- Use a strong password that you can remember;
- Do not store the wallet password together with the seed phrase.

## 【7. Verify the Wallet Address】

After the wallet is restored, the system will show the restored wallet address.

It is recommended to verify whether the address matches your original wallet address.

## 【8. Whether to Display the Wallet Seed】

The system may ask whether you want to display the wallet seed.

For safety, it is recommended to enter:

`N`

Then press Enter.

## 【9. Restore from Blockchain Height】

The system may show:

`Restore from specific blockchain height (optional, default 0)`

This means you can choose a specific blockchain height to start wallet scanning from.

If you are not sure, simply press Enter and use the default value `0`.

This may take longer, but it is safer because the wallet will scan from the beginning.

## 【10. Wait for Wallet Refresh and Balance Recovery】

The wallet will start scanning and synchronizing with the blockchain.

If needed, you can manually refresh the wallet by running:

`refresh`

To check wallet balance, run:

`balance`

After synchronization is completed, the wallet will show available balance and locked balance.

## 【Security Reminders】

Please pay attention to the following security rules:

- Do not share the 25-word seed phrase with anyone;
- Do not share your wallet password;
- Do not share wallet files;
- Do not share view keys or spend keys;
- Do not take screenshots of your seed phrase;
- Do not store the seed phrase in a normal document on an online computer;
- Do not download wallets from unknown links;
- Always download the CLI wallet from the official website;
- Keep the seed phrase offline;
- Use encrypted storage for important backups;
- Keep at least two offline backups in separate safe locations.

## 【Conclusion】

This guide provides a Windows CLI wallet recovery process for restoring an existing Judecoin wallet using the original 25-word seed phrase.

Clear wallet recovery procedures can help users recover wallet access safely, reduce operational mistakes, and protect important wallet credentials.
