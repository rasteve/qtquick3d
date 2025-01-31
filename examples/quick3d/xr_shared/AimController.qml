// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

import QtQuick
import QtQuick3D
import QtQuick3D.Xr

pragma ComponentBehavior: Bound

XrController {
    id: theController
    poseSpace: XrController.AimPose

    readonly property vector3d pickDirection: Qt.vector3d(0, 0, -1)

    required property QtObject view // an object that has implemented rayPick(pos, dir)
    property XrCursor xrCursor: null
    property int pickStatus: PickResult.Null

    property real yeetDistance: 400

    property real emptyRayLength: 400

    //! [signals]
    signal objectPressed(obj: Model, pos: vector3d, direction: vector3d)
    signal objectHovered(obj: Model)
    signal moved(pos: vector3d, direction: vector3d)
    signal released()
    signal objectGrabbed(obj: Model)
    //! [signals]

    QtObject {
        id: priv
        property QtObject hitObject: null
        property bool isGrabbing: false
        property bool isInteracting: false

        property vector3d offset
        property quaternion rotation
        property Node grabbedObject

        function tryGrab() {
            theController.objectGrabbed(hitObject as Model)
        }

        function doGrab(obj: Model) {
            grabbedObject = obj
            if (grabbedObject) {
                const scenePos = grabbedObject.scenePosition
                const sceneRot = grabbedObject.sceneRotation

                offset = theController.mapPositionFromScene(scenePos)
                rotation = theController.rotation.inverted().times(sceneRot)
                isGrabbing = true
                if (xrCursor)
                    xrCursor.visible = false
            }
        }

        function ungrab() {
            hitObject = grabbedObject
            grabbedObject = null
            isGrabbing = false
        }

        function handlePress() {
            theController.objectPressed(hitObject, theController.scenePosition, theController.forward)
            isInteracting = true
            pickRay.pressed = true
        }

        function handleRelease() {
            isInteracting = false
            theController.released()
            pickRay.pressed = false
        }

        function moveObject() {
            if (grabbedObject) {
                let newPos = theController.scenePosition.plus(theController.rotation.times(offset))
                let newRot = theController.sceneRotation.times(rotation)

                if (grabbedObject.parent) {
                    newPos = grabbedObject.parent.mapPositionFromScene(newPos)
                    newRot = grabbedObject.parent.sceneRotation.inverted().times(newRot)
                }

                grabbedObject.setPosition(newPos)
                grabbedObject.setRotation(newRot)
            }
        }

        function yeet(delta: real) {
            const localForward = Qt.vector3d(0, 0, -1)
            const rayPos = localForward.times(pickRay.length)
            const yeetOffset = offset.minus(rayPos)
            pickRay.length = Math.max(10, Math.min(pickRay.length * (1 + delta/10), theController.yeetDistance))
            offset = yeetOffset.plus(localForward.times(pickRay.length))
        }

        function findObject() {
            const dir = theController.mapDirectionToScene(pickDirection)
            const pickResult = theController.view.rayPick(scenePosition, dir)

            const didHit = pickResult.hitType !== PickResult.Null
            theController.pickStatus = pickResult.hitType

            if (didHit) {
                pickRay.hit = true
                pickRay.length = pickResult.distance * 0.75
                hitObject = pickResult.objectHit
                if (xrCursor) {
                    xrCursor.setPositionAndOrientation(pickResult.scenePosition, pickResult.sceneNormal)
                    xrCursor.visible = true
                }
            } else {
                pickRay.hit = false
                pickRay.length = theController.emptyRayLength
                hitObject = null
                if (xrCursor)
                    xrCursor.visible = false
            }
            theController.objectHovered(hitObject)
        }

        function handleMove() {
            if (isInteracting)
                theController.moved(theController.scenePosition, theController.forward) //### sceneFwd
            else if (isGrabbing)
                moveObject()
            else
                findObject()
        }
    }

    function startGrab(obj: Model) {
        priv.doGrab(obj)
    }

    PickRay {
        id: pickRay
        visible: !priv.isGrabbing
        length: theController.emptyRayLength
    }

    onRotationChanged: {
        priv.handleMove()
    }

    XrInputAction {
        id: grabAction
        hand: theController.controller
        actionId: [XrInputAction.SqueezeValue, XrInputAction.SqueezePressed]
        onPressedChanged: {
            if (pressed) {
                priv.tryGrab()
            } else {
                priv.ungrab()
            }
        }
    }

    XrInputAction {
        id: triggerAction
        hand: theController.controller
        actionId: [XrInputAction.TriggerValue, XrInputAction.TriggerPressed, XrInputAction.IndexFingerPinch]
        onPressedChanged:  {
            if (pressed)
                priv.handlePress()
            else
                priv.handleRelease()
        }
    }

    XrInputAction {
        id: yeetAction
        enabled: theController.yeetDistance > 0
        hand: theController.controller
        actionId: XrInputAction.ThumbstickY
    }

    FrameAnimation {
        id: yeetAnimation
        running: priv.isGrabbing && Math.abs(yeetAction.value) > 0.1
        onTriggered: priv.yeet(yeetAction.value * frameTime * 30)
    }
}
