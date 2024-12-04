// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

import QtQuick
import QtQuick.Controls
import QtQuick.Shapes
import QtQuick.Layouts

import QtQuick3D
import QtQuick3D.Helpers

import QtQuick3D.Xr

pragma ComponentBehavior: Bound

XrView {
    id: xrView
    referenceSpace: XrView.ReferenceSpaceStage

    depthSubmissionEnabled: true

    environment: SceneEnvironment {
        id: sceneEnvironment
        lightProbe: Texture {
            textureData: ProceduralSkyTextureData {
            }
        }
        antialiasingMode: SceneEnvironment.MSAA
        antialiasingQuality: SceneEnvironment.High
        backgroundMode: SceneEnvironment.Color
        clearColor: "skyblue"
        probeHorizon: 0.5
    }

    DirectionalLight {
        eulerRotation.x: -30
        eulerRotation.y: -70
    }

    //! [haptics]
    XrHapticFeedback {
        id: hapticFeedback
        hand: XrHapticFeedback.RightHand
        hapticEffect: XrSimpleHapticEffect {
            amplitude: 0.5
            duration: 30
            frequency: 3000
        }
        property Model prevObj: null
        function handleHover(obj: Model) {
            if (obj && obj !== prevObj)
                start()
            prevObj = obj
        }
    }
    //! [haptics]

    xrOrigin: XrOrigin {
        id: theOrigin

        //! [connections]
        AimController {
            id: rightAim
            controller: XrController.ControllerRight

            onObjectPressed: (obj, pos, dir) => {
                gadgetBox.handlePress(obj, pos, dir)
            }
            onObjectHovered: (obj) => {
                gadgetBox.handleHover(obj)
                hapticFeedback.handleHover(obj)
            }
            onMoved: (pos, dir) => {
                gadgetBox.handleMove(pos, dir)
            }
            onReleased: {
                gadgetBox.handleRelease()
            }

            grabMoveEnabled: !gadgetBox.gadgetActive
        }
        //! [connections]
    }

    GadgetBox {
        id: gadgetBox
    }

    Scene {}
}
