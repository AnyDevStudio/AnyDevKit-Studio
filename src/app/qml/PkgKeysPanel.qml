import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Templates as T

// Collects the keys adk needs for a PKG. Studio never ships or creates any of them, and the two
// secret values are write-only here: once saved they are not shown again.
Rectangle {
    id: panel

    required property StudioController studio

    property string pickingFile

    readonly property int total: 4
    readonly property int ready: total - studio.pkgKeyIssues.length

    color: Theme.panel
    radius: 8
    implicitHeight: content.implicitHeight + 32

    FileDialog {
        id: keyFileDialog
        title: "Choose a PKG key file"
        onAccepted: panel.studio.rememberPkgKeyFile(panel.pickingFile, panel.studio.localPath(selectedFile))
    }

    component KeyFileRow: RowLayout {
        id: fileRow

        required property string which
        required property string label
        required property string hint
        required property string value

        spacing: 8

        Field {
            id: fileField
            Layout.fillWidth: true
            label: fileRow.label
            hint: fileRow.hint
            value: fileRow.value
            maximumLength: 2048
            onCommitted: (text) => panel.studio.rememberPkgKeyFile(fileRow.which, text)
        }

        PillButton {
            Layout.alignment: Qt.AlignBottom
            text: "Choose"
            onClicked: {
                panel.pickingFile = fileRow.which
                keyFileDialog.open()
            }
        }
    }

    component SecretField: ColumnLayout {
        id: secret

        required property string which
        required property string label
        required property bool saved

        spacing: 6

        Text {
            text: secret.label
            color: Theme.muted
            font.family: Theme.body
            font.pixelSize: 14
        }

        T.TextField {
            id: input

            Layout.fillWidth: true
            implicitHeight: 38
            leftPadding: 12
            rightPadding: 12
            verticalAlignment: TextInput.AlignVCenter
            color: Theme.paper
            selectionColor: Theme.signal
            selectedTextColor: Theme.ink
            echoMode: TextInput.Password
            inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
            maximumLength: 64
            font.family: Theme.mono
            font.pixelSize: 15
            Accessible.name: secret.label
            Accessible.passwordEdit: true
            onEditingFinished: {
                if (text.length === 0) return
                panel.studio.rememberPkgSecret(secret.which, text)
                text = ""
            }

            background: Rectangle {
                radius: 8
                color: Theme.ink
                border.width: input.activeFocus ? 2 : 1
                border.color: input.activeFocus ? Theme.signal : Theme.line

                Text {
                    anchors.fill: parent
                    leftPadding: 12
                    rightPadding: 12
                    verticalAlignment: Text.AlignVCenter
                    visible: input.text.length === 0 && !input.activeFocus
                    text: secret.saved ? "Saved for this user" : "32 hexadecimal characters"
                    color: secret.saved ? Theme.good : Theme.muted
                    elide: Text.ElideRight
                    font.family: Theme.body
                    font.pixelSize: 14
                }
            }
        }
    }

    ColumnLayout {
        id: content

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Text {
                text: "PKG keys"
                color: Theme.paper
                font.family: Theme.body
                font.weight: Font.DemiBold
                font.pixelSize: 17
            }

            // One cell per thing adk needs, in the tape style of the stage lane.
            Row {
                spacing: 3
                Layout.alignment: Qt.AlignVCenter

                Repeater {
                    model: panel.total

                    Rectangle {
                        required property int index

                        width: 16
                        height: 6
                        radius: 2
                        color: index < panel.ready ? Theme.good : Theme.raised

                        Behavior on color {
                            ColorAnimation { duration: Theme.ms(180) }
                        }
                    }
                }
            }

            Text {
                text: panel.ready + " of " + panel.total + " ready"
                color: panel.ready === panel.total ? Theme.good : Theme.muted
                font.family: Theme.body
                font.pixelSize: 14
            }

            Item { Layout.fillWidth: true }

            PillButton {
                text: "Forget saved keys"
                visible: panel.studio.pkgPasscodeSaved || panel.studio.pkgImageSeedSaved || panel.studio.pkgPasscodeModuli.length > 0 || panel.studio.pkgMountModulus.length > 0
                onClicked: panel.studio.forgetPkgKeys()
            }
        }

        Text {
            Layout.fillWidth: true
            text: "Studio does not include or create PKG keys. Use your own, and only keys you are allowed to use. The passcode and image seed are kept in your user settings, never in the project or the build log. The PKG format itself contains the image seed, so it ends up in the finished package. Studio cannot tell whether keys are valid for a console."
            color: Theme.muted
            wrapMode: Text.WordWrap
            font.family: Theme.body
            font.pixelSize: 14
            lineHeight: 1.25
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 16
            rowSpacing: 10

            KeyFileRow {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                which: "passcodeModuli"
                label: "Passcode moduli file"
                hint: "7 x 384 bytes"
                value: panel.studio.pkgPasscodeModuli
            }

            KeyFileRow {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                which: "mountModulus"
                label: "Mount modulus file"
                hint: "384 bytes"
                value: panel.studio.pkgMountModulus
            }

            SecretField {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                which: "passcode"
                label: "Passcode"
                saved: panel.studio.pkgPasscodeSaved
            }

            SecretField {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                which: "imageSeed"
                label: "Image seed"
                saved: panel.studio.pkgImageSeedSaved
            }
        }

        Text {
            Layout.fillWidth: true
            // A blocked build already says this in its message below the panel.
            visible: panel.studio.pkgKeyIssues.length > 0 && panel.studio.buildState !== "blocked"
            text: "Still needed: " + panel.studio.pkgKeyIssues.join(" ")
            color: Theme.bad
            wrapMode: Text.WordWrap
            font.family: Theme.body
            font.pixelSize: 13
        }
    }
}
