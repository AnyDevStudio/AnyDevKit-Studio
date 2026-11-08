import QtQuick
import QtQuick.Layouts

Item {
    id: view

    required property StudioController studio

    readonly property string revealLabel: Qt.platform.os === "osx" ? "Show in Finder" : "Show in folder"

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 272
            color: Theme.panel

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12

                Text {
                    text: "Files"
                    color: Theme.paper
                    font.family: Theme.body
                    font.weight: Font.DemiBold
                    font.pixelSize: 17
                    Layout.leftMargin: 10
                }

                FileList {
                    studio: view.studio
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }

                PillButton {
                    text: "Open another project"
                    enabled: !view.studio.building
                    Layout.fillWidth: true
                    onClicked: view.studio.closeProject()
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 36
            spacing: 26

            RowLayout {
                Layout.fillWidth: true
                spacing: 24

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Text {
                        Layout.fillWidth: true
                        text: view.studio.titleName || view.studio.projectName
                        color: Theme.paper
                        elide: Text.ElideRight
                        font.family: Theme.display
                        font.pixelSize: Math.min(76, view.width * 0.066)
                        lineHeight: 0.94
                    }

                    RowLayout {
                        spacing: 28

                        Repeater {
                            model: [
                                { label: "Title ID", value: view.studio.titleId },
                                { label: "Version", value: view.studio.contentVersion },
                            ]

                            Row {
                                required property var modelData

                                spacing: 8

                                Text {
                                    text: parent.modelData.label
                                    color: Theme.muted
                                    font.family: Theme.body
                                    font.pixelSize: 15
                                }

                                Text {
                                    text: parent.modelData.value
                                    color: Theme.paper
                                    font.family: Theme.body
                                    font.weight: Font.Medium
                                    font.pixelSize: 15
                                }
                            }
                        }
                    }
                }

                PillButton {
                    primary: true
                    visible: !view.studio.building
                    text: view.studio.buildState === "idle" ? "Build PS5 package" : "Build again"
                    Layout.alignment: Qt.AlignTop
                    onClicked: view.studio.build()
                }

                PillButton {
                    visible: view.studio.building
                    text: "Cancel build"
                    Layout.alignment: Qt.AlignTop
                    onClicked: view.studio.cancelBuild()
                }
            }

            StageLane {
                stages: view.studio.stages
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 16
                visible: view.studio.buildMessage.length > 0

                Text {
                    Layout.fillWidth: true
                    text: view.studio.buildState === "done" && view.studio.outputName ? view.studio.outputName + " is ready, " + view.studio.outputSize + "." : view.studio.buildMessage
                    color: view.studio.buildState === "done" ? Theme.good : view.studio.buildState === "failed" ? Theme.bad : Theme.muted
                    wrapMode: Text.WordWrap
                    font.family: Theme.body
                    font.weight: Font.Medium
                    font.pixelSize: 17
                }

                PillButton {
                    visible: view.studio.buildState === "done"
                    text: view.revealLabel
                    onClicked: view.studio.revealOutput()
                }

                PillButton {
                    visible: view.studio.buildState === "failed"
                    text: "Copy log"
                    onClicked: view.studio.copyLog()
                }
            }

            LogPanel {
                studio: view.studio
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }
    }
}
