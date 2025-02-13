// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

import QtQuick
import QtQuick3D

Node {
    id: teleporter
    required property QtObject view //any object that has implemented rayPick(pos, dir)
    required property Node originNode
    required property Node cameraNode
    required property Node beamHandle
    property real cameraSnapRotation: 30
    property real xStickValue: 0
    property real yStickValue: 0
    property alias screenVisibility: screenValueFader.value
    property bool targetValid: false
    property color rayHitColor: "green"
    property color rayMissColor: "red"
    property bool teleporting: false
    property int blinkSpeed: 150

    function teleportTo(position) {
        teleporter.teleporting = true
        let offset = cameraNode.scenePosition.minus(originNode.scenePosition)
        let cameraOriginPosition = position.minus(offset)
        cameraOriginPosition.y = position.y

        screenValueFader.blink(()=>{
                                   teleporter.doTeleportation(cameraOriginPosition)
                               },()=>{teleporter.teleporting = false}, teleporter.blinkSpeed)
    }

    function rotateBy(degrees) {
        let r = Quaternion.fromEulerAngles(0, degrees, 0)
        let origin = Qt.vector3d(cameraNode.position.x, 0, cameraNode.position.z)
        let mappedOrigin = originNode.rotation.times(origin).plus(originNode.position)
        let rotatedOrigin = r.times(origin)
        let mappedRO = originNode.rotation.times(rotatedOrigin).plus(originNode.position)
        let delta = mappedRO.minus(mappedOrigin)

        doRotation(originNode.rotation.times(r), originNode.position.minus(delta))
    }

    function rotateRight() {
        rotateBy(-cameraSnapRotation)
    }

    function rotateLeft() {
        rotateBy(cameraSnapRotation)
    }

    signal doTeleportation(var cameraOriginPosition)

    signal doRotation(var cameraOriginRotation, var cameraOriginPosition)

    readonly property bool xPlusRotation: xStickValue > 0.5
    onXPlusRotationChanged: {
        if (xPlusRotation)
            rotateRight()
    }

    readonly property bool xMinusRotation: xStickValue < -0.5
    onXMinusRotationChanged: {
        if (xMinusRotation)
            rotateLeft()
    }

    ValueFader {
        id: screenValueFader
    }

    TeleportTargetIndicator {
        id: targetIndicator
    }

    TeleportBeam {
        id: beamModel
        color: teleporter.targetValid ? teleporter.rayHitColor : teleporter.rayMissColor
    }

    FrameAnimation {
        running: teleporter.yStickValue > 0.7
        onTriggered: {
            teleporter.updateTarget()
        }
        onRunningChanged: {
            if (running) {
                beamModel.show()
            }else {
                beamModel.hide()
                targetIndicator.hide()
                if (teleporter.targetValid)
                    teleporter.teleportTo(targetIndicator.scenePosition)
            }
        }
    }

    function updateTarget() : bool {
        // Not a pure gravity parabola: We want a flatter curve

        let beamPositions = [];
        let pos = beamHandle.scenePosition
        const dx = beamHandle.forward.x
        const dz = beamHandle.forward.z
        const a = Qt.vector3d(dx * 2, -4, dz * 2)
        let d = beamHandle.forward.times(50)
        let index = 0
        let hit = false
        let pickResult = null
        beamPositions.push(Qt.vector3d(pos.x, pos.y, pos.z))
        for (let i = 0; !hit && i < 50; ++i) {
            pickResult = teleporter.view.rayPick(pos, d)
            pos = pos.plus(d)
            d = d.plus(a)
            hit = pickResult.objectHit && pickResult.distance < d.length()
            beamPositions.push(Qt.vector3d(pos.x, pos.y, pos.z))
        }
        beamModel.generate(beamPositions)

        if (pickResult.objectHit && pickResult.sceneNormal.y > 0.9) {
            teleporter.targetValid = true
            targetIndicator.moveTo(pickResult.scenePosition)
            targetIndicator.show()
        } else {
            teleporter.targetValid = false
            targetIndicator.hide()
        }

        return teleporter.targetValid
    }
}
