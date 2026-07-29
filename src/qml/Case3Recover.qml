// =============================================================================
// Judecoin Wallet - Case3Recover.qml (st-072)
//
// Case 3 (Recover from seed) detail page. The OnboardingWizard home grid
// lists four case cards; Case 3 ("Recover from seed") is the only one
// currently enabled in the wizard (st-022 left it on, the others off).
// When clicked, OnboardingWizard logs "delegate to RecoveryManager" but
// does not yet navigate to a detail page (st-072b follow-up will wire
// OnboardingWizard.qml -> Loader { source: "qrc:/qml/Case3Recover.qml" }).
// This file is the page that future st-072b / OnboardingWizard.qml edits
// will navigate into; for st-072 modify the page is exercisable in
// isolation as a qmlscene / preview tool test target.
//
// Layout (single-step wizard, similar shape to Case2Open.qml):
//
//   [Forest Green top bar]
//     <- Back  |  Case 3 - Recover from seed                  [step 1/1]
//
//   [Forest Green step indicator]
//
//   [Centered card]
//     <hero copy>  Paste your 25-word monero seed below. The wallet
//                  will scan the chain from the genesis block (or the
//                  restore-height embedded in the seed if present).
//
//     Recovery seed                  [ TextArea, monospace, 6 rows,
//                                       placeholder "word1 word2 ..."]
//     N / 25 words                   live label under the TextArea
//
//     [ Restore wallet ]                                 primary CTA
//     Submit is disabled until:
//       * the pasted text resolves to exactly 25 words AND
//       * each word is in the 1626-word monero English wordlist.
//
//     <status row>
//       red label "Please enter 25 words"                on wrong count
//       red label "Word N is not in the wordlist"        on wordlist fail
//       red label "Restore failed"                       on RPC fail
//       BusyIndicator + "Scanning block N / total"       during scan
//       ProgressBar { from: 0; to: totalBlocks;
//                     value: scanCurrent }                during scan
//
//     Have an existing .keys file?  Open it instead       <- jumps to Case 2
//
//   [Forest Green bottom bar]
//     <- Back to wizard                              secondary CTA
//
// Behaviour (per st-072 spec_notes):
//   * **Word count**: parsed via String.split(/\s+/).trim() chain on
//     every TextArea.text change (cheap, O(n_words)). The N/25 label
//     updates live. Submit stays disabled while N != 25; the red label
//     "Please enter 25 words" only appears after the user has at least
//     tried to submit (or after they've typed 25+ words with bad
//     wordlist entries - whichever triggers first), so we don't pester
//     them on every keystroke (V5.4.4 #B - "validate only after 25
//     entered").
//   * **Wordlist check**: each word must be lowercase ASCII letters,
//     3..8 chars (matches the monero English wordlist shape). This is
//     the lightweight shape check that runs in QML; the authoritative
//     wordlist check (1626-word binary search) lives in the C++ side
//     (RecoveryManager::validateMnemonic) and runs server-side at
//     submit time. When recoveryManagerObj is wired as a context
//     property in a future sub_task, we will swap _validate() to call
//     recoveryManagerObj.validateMnemonic(seedText) and surface the
//     reason verbatim; for st-072 modify the QML-side shape check is
//     the gate and we log "Case3Recover (st-072): wordlist-shape pass"
//     so a future real-test can confirm the shape check is engaged.
//   * **Submit**: when 25-shape-valid words are entered, the user
//     taps Restore. We simulate the recovery flow with a local Timer
//     (5 ticks of 200ms each = 1s of fake scan progress) and emit
//     walletReady("recovered-wallet") on the last tick. When
//     recoveryManagerObj is wired we will replace the simulation with
//     a real recoveryManagerObj.startRecoveryAsync({filename, password,
//     mnemonic}) call and bind the ProgressBar.value to
//     recoveryManagerObj.progressCurrent / recoveryManagerObj.progressTotal.
//     For st-072 modify we keep the simulation so the page is exercisable
//     end-to-end without a live wallet-rpc.
//   * **error**: word-count-wrong -> "Please enter 25 words"; wordlist
//     fail -> "Word N is not in the wordlist"; submit-fail (RPC throw
//     path) -> "Restore failed". We never surface the underlying
//     RpcException::toString() (V5.4.7).
//   * **Have a .keys file?**: link + jump-to-Case-2 button emitting
//     switchToOpenClicked(). The wizard can re-emit this so the user
//     can swap to Case2Open.qml without re-typing their seed.
//
// V5.4.7 compliance: the seed string lives only as a transient QML
// property on the page root (`seedText`). It is wiped on:
//   (a) successful submit (right before walletReady emission),
//   (b) submit failure,
//   (c) Back navigation (top + bottom bar),
//   (d) switchToOpenClicked() (jump to Case 2),
//   (e) Component.onDestruction.
// We do NOT log the seed bytes anywhere. The only console.log lines
// emitted are the st-072 markers ("submit", "scan tick", "walletReady")
// used by the upcoming real-test harness.
//
// V5.4.9 compliance: pure QML, no Widgets; every visible string is
// qsTr() wrapped.
//
// V5.4.15 compliance: single Forest Green theme baked in (#16a34a top
// + bottom bar, green primary CTA). No theme switch.
//
// V5.4.21 compliance: mainnet-only; no regtest/testnet/stagenet UI.
//
// Public API:
//   * signal back()                          -> wizard home
//   * signal walletReady(string filename)    -> wizard -> main.qml
//   * signal switchToOpenClicked()           -> wizard -> Case 2
// =============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "Case3Recover"

    Accessible.role: Accessible.Pane
    Accessible.name: (i18nManagerObj.qtr("Case3Recover", "Case 3 - Recover from seed") + i18nManagerObj.emptyString)

    // ---- public API----------------------------------------------------
    readonly property string pageName: "Case3Recover"
    signal back()
    signal walletReady(string filename)
    signal switchToOpenClicked()

    // ---- injected from context properties (set in main.cpp) -----------
    // RecoveryManager (st-012) owns the Case 3 recovery flow
    // (validateMnemonic + startRecovery[Async]). It is referenced via
    // the recoveryManagerObj context property (same null-guard pattern
    // as OnboardingWizard.qml / Case4BackupRestore.qml) so the QML
    // page compiles even when the C++ side has not yet been wired
    // (recovered at runtime as typeof X !== "undefined"). When the
    // context property is null the page falls back to the local
    // shape-check validation + simulated scan progress (see _submit).
    readonly property var recoveryManager: typeof recoveryManagerObj !== "undefined"
        ? recoveryManagerObj : null

    // ---- form state ----------------------------------------------------
    /// The pasted seed. V5.4.7: TRANSIENT. Wiped on every exit path
    /// below; never logged via console.log. We do not retain the seed
    /// string across navigations within the page.
    property string seedText: ""

    // st-130 (2026-07-17): 25 individual word fields replace the
    // single TextArea. Each field holds one seed word; pasting a
    // multi-word block (e.g. comma-separated seed copied from
    // wallet software) parses and distributes words across all 25
    // fields, avoiding the QML/C++ split inconsistency.
    property var _seedWords: []

    // st-130: rebuild-in-progress guard so _onWordFieldChanged
    // doesn't re-enter _rebuildSeedFromGrid during a multi-word
    // paste distribution.
    property bool _buildingSeed: false

    /// Live word count. Updated on every field change via
    /// _rebuildSeedFromGrid → _parseWords(); the N/25 label and
    /// the Submit gate both read this property.
    property int wordCount: 0

    /// Validation outcomes. We surface these as separate QML
    /// properties (rather than a single reason string) so the page
    /// can render the right label AND keep the Submit gate clean.
    /// True iff every parsed word is 3..8 lowercase ASCII letters
    /// (the monero English wordlist shape; see comment in _validate).
    property bool wordlistShapeOk: false

    /// 1-based index of the first word that failed the wordlist shape
    /// check; 0 if all 25 passed. Drives the red "Word N is not in the
    /// wordlist" label.
    property int firstBadWordIndex: 0

    /// Red error label text for the word-count path. Per st-072 spec:
    /// "Please enter 25 words" only when the user has actually typed
    /// something (we don't pester on a fresh empty page).
    property string wordCountError: ""

    /// Red error label text for the wordlist-shape path. Per st-072
    /// spec: "Word N is not in the wordlist" carrying the 1-based
    /// index of the first failing word.
    property string wordlistError: ""

    /// Red error label text for the submit/RPC failure path. Per
    /// V5.4.7 we never surface the underlying RpcException::toString().
    property string errorMessage: ""

    /// Soft status text (non-error). e.g. "Scanning block N / total".
    property string statusMessage: ""

    /// In-flight flag. True while we are simulating the chain scan
    /// (or while a real RPC is in flight once recoveryManagerObj is
    /// wired). Disables Submit / Back / Open-instead so a stray click
    /// cannot race the wallet-rpc reply.
    property bool inFlight: false

    /// Scan progress. 0..totalBlocks during the chain scan; once the
    /// scan reaches totalBlocks the page emits walletReady(filename)
    /// and flips to phase=complete. st-112 replaces the local-Timer
    /// simulation with the real RecoveryManager signal bindings, so
    /// these properties are now driven by progressUpdated(int, int)
    /// signals — see the Connections element near _submit().
    property int scanCurrent: 0
    /// Fake "total" for the simulation. 2500 ≈ enough ticks to feel
    /// real but still finish in ~1s on a modern CPU. Real wallet-rpc
    /// reports the daemon's chain height which can be millions; we
    /// keep the simulation bounded so real-test can finish fast.
    ///
    /// st-112: this default is also the "no progress signal yet"
    /// fallback for the ProgressBar.to binding. The Connections
    /// element re-binds scanTotalHint to whatever the latest
    /// progressUpdated(current, total) reports, and the ProgressBar
    /// uses progressTotal > 0 ? progressTotal : 2500 below. The
    /// 2500 default is intentionally preserved so a QML preview
    /// without a live recoveryManagerObj still renders something.
    property int scanTotalHint: 0
    /// Convenience alias used by the ProgressBar.to binding:
    /// scanTotalHint > 0 ? scanTotalHint : 2500.
    readonly property int scanTotal:
        page.scanTotalHint > 0 ? page.scanTotalHint : 2500

    /// Phase indicator. 0 = input, 1 = scanning, 2 = complete.
    /// Drives which sub-section of the centered card is visible.
    /// Transitions: 0 -> 1 (Submit), 1 -> 2 (recoveryComplete
    /// signal fires), 2 -> 0 (Back / new recovery).
    property int phase: 0

    /// Convenience bools. submitEnabled mirrors Case2Open's
    /// submitEnabled shape (filename + password non-empty + not in
    /// flight) so the existing test patterns can grep for "submit
    /// enabled = true" markers uniformly across cases.
    readonly property bool countOk: page.wordCount === 25

    /// V5.4.7: error message shown when the recovery RPC fails.
    /// Kept as a property so the (i18nManagerObj.qtr("Case3Recover", "Restore failed") + i18nManagerObj.emptyString) literal lives
    /// in the source for the st-072 source-search contract; the
    /// local-Timer simulation in _submit() never sets this (it cannot
    /// fail), so the value stays "" until recoveryManagerObj is wired
    /// and a future onError slot assigns page.errorMessage = page.
    /// restoreFailedText.
    readonly property string restoreFailedText: (i18nManagerObj.qtr("Case3Recover", "Restore failed") + i18nManagerObj.emptyString)
    readonly property bool submitEnabled: {
        return page.countOk
            && page.wordlistShapeOk
            && !page.inFlight
            && page.phase === 0
            && page.recoveryPassword.length > 0
    }

    // =====================================================================
    // UI
    // =====================================================================
    ColumnLayout {
        anchors.fill: page
        spacing: 0

        // ---- Forest Green top bar -------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: "#16a34a"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 12

                Button {
                    objectName: "Case3BackButton"
                    text: (i18nManagerObj.qtr("Case3Recover", "\u2190 Back") + i18nManagerObj.emptyString)
                    Accessible.role: Accessible.Button
                    Accessible.name: (i18nManagerObj.qtr("Case3Recover", "Back to wizard home") + i18nManagerObj.emptyString)
                    onClicked: {
                        page._wipeSecrets()
                        page.back()
                    }
                }
                Label {
                    text: (i18nManagerObj.qtr("Case3Recover", "Case 3 - Recover from seed") + i18nManagerObj.emptyString)
                    color: "#ffffff"
                    font.pixelSize: 20
                    font.bold: true
                    Layout.fillWidth: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
                Label {
                    text: (i18nManagerObj.qtr("Case3Recover", "Step 1 / 1") + i18nManagerObj.emptyString)
                    color: "#dcfce7"
                    font.pixelSize: 12
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
            }
        }

        // ---- Forest Green step indicator ------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            color: "#f0fdf4"

            RowLayout {
                anchors.centerIn: parent
                spacing: 8

                Repeater {
                    model: 1
                    delegate: Rectangle {
                        required property int index
                        width: 28
                        height: 6
                        radius: 3
                        color: page.phase === 1 ? "#ca8a04" : "#16a34a"
                    }
                }
            }
        }

        // ---- Centered card -------------------------------------------
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Rectangle {
                id: card
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 560)
                height: cardLayout.implicitHeight + 48
                radius: 10
                border.color: (page.errorMessage.length > 0
                               || page.wordCountError.length > 0
                               || page.wordlistError.length > 0)
                    ? "#dc2626" : "#bbf7d0"
                border.width: 1
                color: "#ffffff"

                ColumnLayout {
                    id: cardLayout
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 14

                    // ---- Hero copy -------------------------------------
                    Label {
                        text: (i18nManagerObj.qtr("Case3Recover", "Recover from seed") + i18nManagerObj.emptyString)
                        font.pixelSize: 22
                        font.bold: true
                        color: "#14532d"
                        Accessible.role: Accessible.Heading
                        Accessible.name: text
                    }
                    Label {
                        text: qsTr(
                            "Paste your 25-word monero seed below. The wallet " +
                            "will scan the chain to find your past transactions " +
                            "and balance. Your seed never leaves this device.")
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        color: "#6b8074"
                        font.pixelSize: 12
                        Accessible.role: Accessible.StaticText
                        Accessible.name: text
                    }

                    // ---- Seed TextArea + live N/25 label --------------
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: (i18nManagerObj.qtr("Case3Recover", "Recovery seed") + i18nManagerObj.emptyString)
                                font.pixelSize: 12
                                color: "#6b8074"
                                Layout.fillWidth: true
                            }
                            Label {
                                objectName: "Case3WordCountLabel"
                                text: (i18nManagerObj.qtr("Case3Recover", "%1 / 25 words").arg(page.wordCount) + i18nManagerObj.emptyString)
                    font.pixelSize: 12
                                color: page.countOk ? "#16a34a" : "#6b8074"
                                font.bold: page.countOk
                            }
                        }

                        // ---- Seed word grid: 5 × 5 = 25 fields ------
                        // st-130: each field holds one word. Pasting a
                        // multi-word block (space/comma-separated)
                        // into any field parses and distributes words
                        // across all 25 fields, eliminating the QML/C++
                        // delimiter mismatch.
                        GridLayout {
                            id: seedGrid
                            Layout.fillWidth: true
                            columns: 5
                            columnSpacing: 6
                            rowSpacing: 6
                            readonly property int fieldHeight: 36

                            Repeater {
                                id: seedRepeater
                                model: 25
                                TextField {
                                    // st-130: capture Repeater index in a
                                    // delegate property so onTextChanged
                                    // signal handler can reference it.
                                    readonly property int wordIdx: index

                                    Layout.fillWidth: true
                                    Layout.preferredHeight: seedGrid.fieldHeight
                                    font.family: "monospace"
                                    font.pixelSize: 13
                                    // st-130: NO maximumLength here — a
                                    // user pasting a 25-word seed must
                                    // reach the full text in onTextChanged
                                    // so _onWordFieldChanged can detect
                                    // the multi-word paste and distribute
                                    // words via _distributePaste.
                                    // Word-length enforcement (3-8 chars)
                                    // happens in _onWordFieldChanged + the
                                    // existing _parseWords()/_isShapeOk().
                                    color: "#14532d"
                                    placeholderText: (index + 1).toString()
                                    placeholderTextColor: "#9ca3af"
                                    horizontalAlignment: TextInput.AlignHCenter
                                    selectByMouse: true
                                    text: {
                                        if (!page._seedWords) return ""
                                        return page._seedWords[wordIdx] || ""
                                    }
                                    enabled: !page.inFlight && page.phase === 0
                                    background: Rectangle {
                                        radius: 4
                                        color: "#ffffff"
                                        border.color: parent.activeFocus
                                            ? "#22c55e"
                                            : (page.wordlistError.length > 0
                                                ? "#dc2626"
                                                : "#d1fae5")
                                        border.width: 1
                                    }
                                    Accessible.name: (i18nManagerObj.qtr("Case3Recover", "Seed word %1")
                                        .arg(index + 1) + i18nManagerObj.emptyString)
                    onTextChanged: {
                                        if (page._buildingSeed) return
                                        page._onWordFieldChanged(wordIdx, text)
                                    }

                                    // Auto-advance to next field on space
                                    Keys.onPressed: function(event) {
                                        if (event.key === Qt.Key_Space
                                            && wordIdx < 24) {
                                            event.accepted = true
                                            var next = seedRepeater
                                                .itemAt(wordIdx + 1)
                                            if (next) next.forceActiveFocus()
                                        }
                                    }
                                }
                            }
                        }

                        // ---- Word-count error (only after first attempt) -
                        // st-130: word count determined by filled grid
                        // fields, not TextArea split.
                        Label {
                            objectName: "Case3WordCountErrorLabel"
                            Layout.fillWidth: true
                            visible: page.wordCountError.length > 0
                            text: page.wordCountError
                            color: "#dc2626"
                            font.pixelSize: 12
                            font.bold: true
                            wrapMode: Text.WordWrap
                        }

                        // ---- Wordlist-shape error ----------------------
                        Label {
                            objectName: "Case3WordlistErrorLabel"
                            Layout.fillWidth: true
                            visible: page.wordlistError.length > 0
                            text: page.wordlistError
                            color: "#dc2626"
                            font.pixelSize: 12
                            font.bold: true
                            wrapMode: Text.WordWrap
                        }
                    }

                    // st-152 (2026-07-24): wallet name field so the
                    // user can set a custom name when recovering from seed.
                    // Previously recoveryFilename was hardcoded to
                    // "wallet-" + Date.now().
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        visible: page.phase === 0
                        Label {
                            text: (i18nManagerObj.qtr("Case3Recover", "Wallet name") + i18nManagerObj.emptyString)
                            font.pixelSize: 12
                            color: "#6b8074"
                        }
                        TextField {
                            id: recoveryNameField
                            objectName: "Case3RecoveryNameField"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 40
                            placeholderText: page._defaultWalletName
                            font.pixelSize: 14
                            onTextChanged: page.recoveryName = text
                            Accessible.role: Accessible.EditableText
                            Accessible.name: (i18nManagerObj.qtr("Case3Recover", "Wallet name") + i18nManagerObj.emptyString)
                        }
                        Label {
                            text: page.recoveryName.length > 0
                                ? (i18nManagerObj.qtr("Case3Recover", "Wallet will be saved as: %1.keys").arg(page.recoveryFilename) + i18nManagerObj.emptyString)
                                : (i18nManagerObj.qtr("Case3Recover", "Leave empty to use default name") + i18nManagerObj.emptyString)
                            font.pixelSize: 11
                            color: "#6b8074"
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }

                    // FIX-G11 (2026-07-18): password field for the
                    // recovered wallet. Visible during seed entry (phase 0).
                    // The password is set on the page's recoveryPassword
                    // property and used in _submit()'s recovery config.
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        visible: page.phase === 0
                        Label {
                            text: (i18nManagerObj.qtr("Case3Recover", "Wallet password") + i18nManagerObj.emptyString)
                            font.pixelSize: 12
                            color: "#6b8074"
                        }
                        TextField {
                            id: recoveryPasswordField
                            objectName: "Case3RecoveryPasswordField"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 40
                            placeholderText: (i18nManagerObj.qtr("Case3Recover", "Set a password for the recovered wallet") + i18nManagerObj.emptyString)
                            echoMode: TextInput.Password
                            font.pixelSize: 14
                            onTextChanged: page.recoveryPassword = text
                            Accessible.role: Accessible.EditableText
                            Accessible.name: (i18nManagerObj.qtr("Case3Recover", "Wallet password") + i18nManagerObj.emptyString)
                        }
                    }

                    // ---- Scan progress (visible during phase 1) --------
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        visible: page.phase === 1

                        BusyIndicator {
                            Layout.alignment: Qt.AlignHCenter
                            running: page.phase === 1
                            visible: page.phase === 1
                        }
                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            objectName: "Case3ScanStatusLabel"
                            text: page.statusMessage.length > 0
                                ? page.statusMessage
                                : (i18nManagerObj.qtr("Case3Recover", "Scanning block %1 / %2")
                                    .arg(page.scanCurrent)
                                    .arg(page.scanTotal) + i18nManagerObj.emptyString)
                    color: "#14532d"
                            font.pixelSize: 12
                        }
                        ProgressBar {
                            objectName: "Case3ScanProgressBar"
                            Layout.fillWidth: true
                            from: 0
                            to: page.scanTotal
                            value: page.scanCurrent
                            Accessible.role: Accessible.ProgressBar
                            Accessible.name: (i18nManagerObj.qtr("Case3Recover", "Chain scan progress") + i18nManagerObj.emptyString)
                        }
                    }

                    // ---- Generic submit error -------------------------
                    Label {
                        objectName: "Case3ErrorLabel"
                        Layout.alignment: Qt.AlignHCenter
                        Layout.fillWidth: true
                        text: page.errorMessage
                        visible: page.errorMessage.length > 0
                        color: "#dc2626"
                        font.pixelSize: 13
                        font.bold: true
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                    }

                    // ---- Submit button ---------------------------------
                    Button {
                        objectName: "Case3SubmitButton"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 44
                        text: page.inFlight
                            ? (i18nManagerObj.qtr("Case3Recover", "Restoring wallet...") + i18nManagerObj.emptyString)
                            : (i18nManagerObj.qtr("Case3Recover", "Restore wallet") + i18nManagerObj.emptyString)
                        enabled: page.submitEnabled
                        onClicked: page._submit()
                        Accessible.role: Accessible.Button
                        Accessible.name: text
                    }

                    // ---- Have a .keys file? link ----------------------
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        spacing: 6

                        Item { Layout.fillWidth: true }

                        Label {
                            text: (i18nManagerObj.qtr("Case3Recover", "Have an existing .keys file?") + i18nManagerObj.emptyString)
                            color: "#6b8074"
                            font.pixelSize: 12
                        }
                        Button {
                            objectName: "Case3OpenInsteadButton"
                            text: (i18nManagerObj.qtr("Case3Recover", "Open it instead") + i18nManagerObj.emptyString)
                            enabled: !page.inFlight
                            Accessible.role: Accessible.Button
                            Accessible.name: text
                            onClicked: {
                                page._wipeSecrets()
                                page.switchToOpenClicked()
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }
                }
            }
        }

        // ---- Forest Green bottom bar ----------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: "#f7fdf9"
            border.color: "#bbf7d0"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 12

                Button {
                    text: (i18nManagerObj.qtr("Case3Recover", "\u2190 Back to wizard") + i18nManagerObj.emptyString)
                    enabled: !page.inFlight
                    Accessible.role: Accessible.Button
                    Accessible.name: (i18nManagerObj.qtr("Case3Recover", "Back to wizard") + i18nManagerObj.emptyString)
                    onClicked: {
                        page._wipeSecrets()
                        page.back()
                    }
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: (i18nManagerObj.qtr("Case3Recover", "V5.4.7 \u00b7 seed never leaves the device") + i18nManagerObj.emptyString)
                    color: "#6b8074"
                    font.pixelSize: 11
                }
            }
        }
    }

    // =====================================================================
    // st-130: word-field change handler (replaces old TextArea.onTextChanged).
    // Called by each of the 25 TextField delegates when text changes.
    // =====================================================================
    function _onWordFieldChanged(wordIdx, newText) {
        if (page._buildingSeed) return

        // Detect multi-word paste: text contains whitespace / comma.
        if (/[\s,]/.test(newText)) {
            page._distributePaste(newText, wordIdx)
            return
        }

        // Single-word input. Truncate to 8 chars (monero word limit).
        if (!page._seedWords || page._seedWords.length !== 25) return
        var clean = newText.toLowerCase()
        if (clean.length > 9) clean = clean.substring(0, 9)
        page._seedWords[wordIdx] = clean
        page._seedWords = page._seedWords  // trigger binding refresh
        page._rebuildSeedFromGrid()
    }

    // st-130: parse a multi-word paste (space/comma-separated seed).
    // Distribute words across all 25 fields, filling from index 0.
    function _distributePaste(pastedText, sourceIdx) {
        page._buildingSeed = true
        var raw = pastedText.toLowerCase().split(/[\s,]+/)
        var words = raw.filter(function(w) { return w.length > 0 })
        var newWords = []
        for (var i = 0; i < 25; i++) {
            newWords.push(i < words.length ? words[i] : "")
        }
        page._seedWords = newWords
        page._buildingSeed = false
        page._rebuildSeedFromGrid()
        // st-130: sourceIdx unused by design (paste fills from field 0);
        // qmllint rejects (void)expr so we just use the param in a
        // no-op to suppress the "unused parameter" warning.
        console.debug("st-130: _distributePaste sourceIdx=", sourceIdx)
    }

    // st-130: read all 25 field values, join into seedText, re-run
    // parse + validation. Called after every field change.
    function _rebuildSeedFromGrid() {
        if (!page._seedWords || page._seedWords.length !== 25) return
        var filled = page._seedWords.filter(function(w) {
            return w && w.length > 0
        })
        page.seedText = filled.join(" ")
        page._parseWords()
    }

    // =====================================================================
    // Parse + validate helpers (st-130: adapted for grid input)
    // Reads seedText (built by _rebuildSeedFromGrid above) and
    // updates wordCount / wordlistShapeOk / firstBadWordIndex.
    // =====================================================================

    // Split seedText into a list of words (lowercase, trimmed, no empties).
    // Re-runs after every grid field change via _rebuildSeedFromGrid.
    // Updates page.wordCount and the N/25 label; runs the lightweight
    // wordlist-shape check (lowercase ASCII letters, length 3..8).
    function _parseWords() {
        // V5.4.7: do NOT lowercase before the shape check - we want
        // uppercase letters in any word to fail /^[a-z]{3,8}$/ and
        // surface the "Word %1 is not in the wordlist" red label so
        // the user knows their paste is bad rather than silently
        // accepting mixed-case.
        var raw = (page.seedText || "")
        // Split on whitespace OR comma (one or more of either).
        // st-130: seedText is built from space-joined grid fields
        // which are already lowercase, so the comma path is a
        // belt-and-suspenders fallback.
        var tokens = raw.split(/[\s,]+/)
        var words = []
        for (var i = 0; i < tokens.length; ++i) {
            var t = tokens[i]
            if (t && t.length > 0) words.push(t)
        }
        page.wordCount = words.length

        // Lightweight shape check: each word 3..8 chars, all lowercase
        // ASCII letters. Matches the monero English wordlist contract
        // (MnemonicWordList::english()). The authoritative check (binary
        // search against the 1626-word list) lives in RecoveryManager
        // and is invoked at submit time.
        var shapeOk = true
        var firstBad = 0
        for (var j = 0; j < words.length; ++j) {
            var w = words[j]
            if (!_isShapeOk(w)) {
                shapeOk = false
                firstBad = j + 1   // 1-based for the user-facing label
                break
            }
        }
        page.wordlistShapeOk = (words.length === 25) ? shapeOk : false
        page.firstBadWordIndex = firstBad

        // Surface the "Word N is not in the wordlist" label only when
        // the user has filled the full 25.
        if (words.length === 25 && !shapeOk && firstBad > 0) {
            page.wordlistError = (i18nManagerObj.qtr("Case3Recover", "Word %1 is not in the wordlist").arg(firstBad) + i18nManagerObj.emptyString)} else {
            page.wordlistError = ""
        }
    }

    // Helper: 3..8 lowercase ASCII letters.
    function _isShapeOk(word) {
        if (typeof word !== "string") return false
        // st-130 (2026-07-18): monero English wordlist has 9-char
        // words (nocturnal, adrenalin, adventure). Raised from
        // {3,8} to {3,9} to match the actual wordlist.
        if (word.length < 3 || word.length > 9) return false
        return /^[a-z]{3,9}$/.test(word)
    }

    // =====================================================================
    // Submit handler
    //
    // st-112: replaced the st-072 local-Timer simulation with a real
    // RecoveryManager call. The contract:
    //
    //   page.recoveryManager.startRecoveryAsync({filename, password,
    //   mnemonic}) returns a correlation id; we then drive the
    //   ProgressBar + status label from the progressUpdated(current,
    //   total) signal (reached via the Connections element below)
    //   and flip to phase=2 on recoveryComplete.
    //
    // We still tolerate a null recoveryManager (e.g. an older binary
    // running the page without the st-112 main.cpp wire-up) by
    // surfacing the errorMessage and keeping Submit disabled while
    // inFlight, but the local-Timer simulation is REMOVED — V5.4
    // §10 says pre-production simulators must not coexist with the
    // real RPC path or downstream tests will pass against the wrong
    // surface.
    //
    // V5.4.7: the seedText property is wiped immediately before the
    // walletReady signal fires (and on every error path), so the
    // post-success QML tree holds no copy of the seed string.
    // =====================================================================
    function _submit() {
        if (!page.submitEnabled) return

        // Sanity: re-validate right at submit time so a paste that
        // looks valid by the gate can't slip through if the user
        // edited the TextArea in the same tick.
        page._parseWords()
        if (!page.countOk) {
            page.wordCountError = (i18nManagerObj.qtr("Case3Recover", "Please enter 25 words") + i18nManagerObj.emptyString)
            return
        }
        if (!page.wordlistShapeOk) {
            // First-bad-word index was already computed by _parseWords.
            page.wordlistError = (i18nManagerObj.qtr("Case3Recover", "Word %1 is not in the wordlist")
                .arg(page.firstBadWordIndex) + i18nManagerObj.emptyString)
                    return
        }
        page.wordCountError = ""
        page.wordlistError = ""

        if (page.recoveryManager === null) {
            // Defensive: the st-112 main.cpp wire-up always sets
            // recoveryManagerObj, so this branch should be unreachable
            // in production. Surface a red error label so a regression
            // is obvious: the user sees "recoveryManagerObj not
            // available" rather than a silent no-op.
            page.errorMessage = (i18nManagerObj.qtr("Case3Recover", "Recovery manager unavailable (main.cpp did not wire recoveryManagerObj).") + i18nManagerObj.emptyString)
            console.warn("Case3Recover (st-112): recoveryManagerObj " +
                         "is undefined - check main.cpp " +
                         "setContextProperty(\"recoveryManagerObj\", ...)")
            return
        }

        page.inFlight = true
        page.phase = 1
        page.errorMessage = ""
        page.statusMessage = (i18nManagerObj.qtr("Case3Recover", "Starting recovery…") + i18nManagerObj.emptyString)
        page.scanCurrent = 0
        // V5.4 §10: clear the old scanTotal (was 2500 in the sim
        // contract) so a stale progress bar value from a previous
        // attempted recovery does not leak into the new attempt.
        // The progressUpdated signal below will re-set scanTotal
        // to the daemon's chain height as soon as the polling
        // timer fires.
        page.scanTotalHint = 0

        var config = {
            filename: page.recoveryFilename,
            password: page.recoveryPassword,
            mnemonic: page.seedText
        }
        var correlationId = "case3-" + Date.now()
        console.log("Case3Recover (st-112): submit wordCount=25, " +
                    "shapeOk=true -> calling recoveryManagerObj." +
                    "startRecoveryAsync correlationId=", correlationId)
        // st-112 Bug #1 (2026-07-17): set _currentCid BEFORE the
        // startRecoveryAsync call so synchronous recoveryFailed /
        // recoveryComplete emissions inside the call can match via
        // the onRecovery* signal handlers. Prior code set it AFTER
        // the call, which caused the handler to drop early signals
        // (cid !== "" → dropped → page stuck at phase=1 forever).
        page._currentCid = correlationId
        var cid = page.recoveryManager.startRecoveryAsync(config, correlationId)
        // If the RPC returned a different cid, use it; otherwise
        // keep the one we set above.
        if (cid && cid.length > 0 && cid !== correlationId) {
            page._currentCid = cid
        }
    }

    // -- st-112: real RecoveryManager progress + completion signals ----
    // Wire the C++ side's progressUpdated(int current, int total) +
    // recoveryComplete(filename, openedAtMs, correlationId) +
    // recoveryFailed(reason, rpcMethod, correlationId) signals into
    // the QML page's scanCurrent / scanTotal / scanTotalHint /
    // phase / walletReady emission.
    //
    // V5.4.7: signals arrive on the GUI thread, so we can write
    // QML properties directly without marshalling.
    Connections {
        target: page.recoveryManager
        // ignoreUnknownSignals so the page still compiles against a
        // build of RecoveryManager that has not yet landed the
        // st-112 progressCurrent / progressTotal Q_PROPERTY surface
        // (the C++ class exposes them as Q_PROPERTY+NOTIFY in this
        // commit, so the early-out never fires; the guard just
        // keeps the page tolerant).
        ignoreUnknownSignals: true

        function onProgressUpdated(current, total) {
            // Real wallet-rpc progress: current = scanned height,
            // total = daemon chain height. The page's ProgressBar
            // binds scanCurrent / scanTotalHint so the bar fills
            // monotonically across the scan.
            page.scanCurrent = current
            page.scanTotalHint = total
            page.statusMessage = (i18nManagerObj.qtr("Case3Recover", "Scanning block %1 / %2")
                .arg(current).arg(total) + i18nManagerObj.emptyString)}
        function onRecoveryComplete(filename, openedAtMs, cid) {
            if (cid !== page._currentCid) {
                // Stale reply (e.g. user pressed back during a
                // recover). Drop it on the floor; do NOT emit
                // walletReady, do NOT flip phase.
                return
            }
            page.statusMessage = (i18nManagerObj.qtr("Case3Recover", "Wallet recovered.") + i18nManagerObj.emptyString)
            page.inFlight = false
            page.phase = 2
            // V5.4.7: scrub the seed before emitting walletReady.
            page._wipeSecrets()
            console.log("Case3Recover (st-112): recoveryComplete " +
                        "filename=", filename, "openedAtMs=", openedAtMs,
                        "-> emitting walletReady")
            page.walletReady(filename)
        }
        function onRecoveryFailed(reason, rpcMethod, cid) {
            if (cid !== page._currentCid) {
                // Stale reply, drop.
                return
            }
            // st-112 (2026-07-17): when rpcMethod is empty the
            // failure is a local validation error (mnemonic
            // wordlist / backup config check) — show the reason
            // verbatim so the user knows what's wrong.
            // When rpcMethod is set it's an RPC error — generic
            // label per V5.4.7 (never surface RpcException details).
            if (rpcMethod && rpcMethod.length > 0) {
                page.errorMessage = page.restoreFailedText +
                    " (" + rpcMethod + ")"
            } else if (reason && reason.length > 0) {
                page.errorMessage = reason
            } else {
                page.errorMessage = page.restoreFailedText
            }
            page.statusMessage = ""
            page.inFlight = false
            page.phase = 0
            console.warn("Case3Recover (st-112): recoveryFailed " +
                         "cid=", cid, "rpcMethod=", rpcMethod,
                         "reason=", reason)
        }
    }

    // st-152 (2026-07-24): wallet name is now user-supplied through
    // a TextField in the seed-entry phase (phase=0). Default name is
    // "wallet-" + Date.now() so re-restoring the same seed never hits
    // "Wallet already exists" when the user leaves the field empty.
    readonly property string _defaultWalletName: "wallet-" + Date.now()
    property string recoveryName: ""
    readonly property string recoveryFilename: page.recoveryName.length > 0
        ? page.recoveryName : page._defaultWalletName
    property string recoveryPassword: ""
    /// Correlation id of the in-flight startRecoveryAsync call.
    /// The Connections handlers above match recoveryComplete /
    /// recoveryFailed signals against this so a stale reply from
    /// an earlier submit (or a background poll tick) cannot flip
    /// the page into phase=complete spuriously.
    property string _currentCid: ""

    // =====================================================================
    // V5.4.7: scrub transient secret material on every exit path. The
    // page root may be torn down by:
    //   * back() emission (top + bottom Back button) -> _wipeSecrets()
    //   * switchToOpenClicked() (Open-it-instead)    -> _wipeSecrets()
    //   * StackView destroy (root destroy)           -> Component.onDestruction
    //   * submit success                              -> _wipeSecrets()
    // We do NOT scrub on submit failure (the user may want to fix
    // and retry); we DO scrub on any navigation away.
    //
    // scanCurrent is intentionally NOT scrubbed: on the success path
    // the ProgressBar must remain at scanTotal (100% complete) so
    // the UI shows the recovered state; on every other exit path
    // the page is being torn down so the residual value is
    // invisible.
    // =====================================================================
    function _wipeSecrets() {
        // st-112 Bug #3 (2026-07-17): cancel any in-flight
        // RecoveryManager work before scrubbing QML state.
        // Without this, a stale non-Idle C++ state survives
        // page re-entry, causing the next startRecoveryAsync
        // call to synchronously emit recoveryFailed before
        // _currentCid is set → silent drop → page hangs.
        if (page.recoveryManager !== null) {
            try {
                page.recoveryManager.cancel()
            } catch (e) {
                // Fail-safe: if cancel() throws, scrub QML state
                // anyway so the next attempt has a clean slate.
                console.warn("Case3Recover: recoveryManager.cancel() " +
                             "threw:", e)
            }
        }
        // V5.4.7: scrub transient secret material (seedText,
        // wordCount, shape flags, error labels) - NOT phase, NOT
        // scanCurrent. phase is a UI state (0=input, 1=scanning,
        // 2=complete); callers that need to reset to phase 0
        // (back, switchToOpenClicked) set it explicitly after this
        // call. The scan-timer success path leaves phase at 2 so
        // main.qml / the wizard can observe walletReady + phase==2
        // together, AND leaves scanCurrent at scanTotal so the
        // ProgressBar shows 100% complete until the user navigates
        // away. _submit() resets page.scanCurrent = 0 at the top
        // of each new recovery attempt.
        page.seedText = ""
        page.wordCount = 0
        page.wordlistShapeOk = false
        page.firstBadWordIndex = 0
        page.wordCountError = ""
        page.wordlistError = ""
        page.errorMessage = ""
        // st-112: scrub the recovery progress hint and correlation
        // id so a stray recoveryComplete from a previous attempt
        // cannot leak into the next attempt's signal handlers.
        page.scanTotalHint = 0
        // statusMessage is intentionally NOT cleared here - it is a
        // user-visible label (e.g. "Wallet recovered." on success,
        // "Scanning block N / total" mid-scan) and the page is
        // expected to retain the last meaningful status so the
        // parent wizard / main.qml can observe it after walletReady.
        // NOTE: page.scanCurrent is intentionally NOT reset here.
        // On the scan-timer success path the progress bar should
        // remain at scanTotal (2500) showing 100% completion when
        // walletReady fires, so the user sees the recovered state
        // until they navigate away. On every other exit path
        // (back / switchToOpenClicked / Component.onDestruction)
        // the page is being torn down so the residual value is
        // invisible. _submit() resets page.scanCurrent = 0 at the
        // top of each new recovery attempt.
        page.inFlight = false
        // st-130: scrub the 25-word grid array so the next page
        // load starts with empty fields.
        page._seedWords = []
        page._buildingSeed = false
        // st-152: scrub the wallet name + password so the next
        // recovery attempt starts fresh.
        page.recoveryName = ""
        page.recoveryPassword = ""
        // st-130: the "seedArea" TextArea no longer exists (replaced
        // by 25 individual TextField inputs in a GridLayout).
        // _seedWords (above) is the authoritative clear for the
        // grid fields via their text: page._seedWords[wordIdx] binding.
        // st-112: scanTimer was removed when we replaced the local
        // simulation with the real RecoveryManager signal bindings
        // (see Connections { onProgressUpdated / onRecoveryComplete }
        // above). page._currentCid is now the source-of-truth for
        // signal match; scrub it too so a back-then-resubmit cannot
        // re-fire a stale recoveryComplete handler.
        page._currentCid = ""
    }

    Component.onCompleted: {
        // st-130: initialise the 25-word array with empty strings
        // so the GridLayout text bindings don't see undefined.
        var arr = []
        for (var i = 0; i < 25; i++) arr.push("")
        page._seedWords = arr
        page._buildingSeed = false
    }

    Component.onDestruction: {
        page._wipeSecrets()
    }
}
