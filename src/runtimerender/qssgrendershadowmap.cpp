// Copyright (C) 2008-2012 NVIDIA Corporation.
// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#include <QtQuick3DRuntimeRender/private/qssgrenderlayer_p.h>
#include <QtQuick3DRuntimeRender/private/qssgrendershadowmap_p.h>
#include <QtQuick3DRuntimeRender/private/qssglayerrenderdata_p.h>
#include "qssgrendercontextcore.h"

QT_BEGIN_NAMESPACE

static constexpr quint32 NUM_TEXTURE_SIZES = 5;

static QRhiTexture *allocateRhiShadowTexture(QRhi *rhi, QRhiTexture::Format format, const QSize &size, quint32 numLayers, QRhiTexture::Flags flags)
{
    auto texture = rhi->newTexture(format, size, 1, flags);
    if (flags & QRhiTexture::TextureArray)
        texture->setArraySize(numLayers);
    if (!texture->create())
        qWarning("Failed to create shadow map texture of size %dx%d", size.width(), size.height());
    return texture;
}

static QRhiRenderBuffer *allocateRhiShadowRenderBuffer(QRhi *rhi, QRhiRenderBuffer::Type type, const QSize &size)
{
    auto renderBuffer = rhi->newRenderBuffer(type, size, 1);
    if (!renderBuffer->create())
        qWarning("Failed to build depth-stencil buffer of size %dx%d", size.width(), size.height());
    return renderBuffer;
}

static QRhiTexture::Format getShadowMapTextureFormat(QRhi *rhi, bool use32bit)
{
    if (use32bit && rhi->isTextureFormatSupported(QRhiTexture::R32F))
        return QRhiTexture::R32F;
    if (rhi->isTextureFormatSupported(QRhiTexture::R16F))
        return QRhiTexture::R16F;
    return QRhiTexture::R16;
}

static quint8 mapSizeToIndex(quint32 mapSize)
{
    Q_ASSERT(!(mapSize & (mapSize - 1)) && "shadow map resolution is power of 2");
    Q_ASSERT(mapSize >= 256);
    quint8 index = qCountTrailingZeroBits(mapSize) - 8;
    Q_ASSERT(index < NUM_TEXTURE_SIZES);
    return index;
}

static quint32 indexToMapSize(quint8 index)
{
    Q_ASSERT(index < NUM_TEXTURE_SIZES);
    return 1 << (index + 8);
}

QSSGRenderShadowMap::QSSGRenderShadowMap(const QSSGRenderContextInterface &inContext)
    : m_context(inContext)
{
}

QSSGRenderShadowMap::~QSSGRenderShadowMap()
{
    releaseCachedResources();
}

void QSSGRenderShadowMap::releaseCachedResources()
{
    for (QSSGShadowMapEntry &entry : m_shadowMapList)
        entry.destroyRhiResources();

    for (auto &hash : m_depthTextureArrays) {
        for (auto &textureArray : hash)
            delete textureArray;
        hash.clear();
    }
    m_shadowMapList.clear();
}

void QSSGRenderShadowMap::addShadowMaps(const QSSGShaderLightList &renderableLights)
{
    QRhi *rhi = m_context.rhiContext()->rhi();
    // Bail out if there is no QRhi, since we can't add entries without it
    if (!rhi)
        return;

    constexpr quint32 MAX_SPLITS = 4;
    const quint32 numLights = renderableLights.size();
    qsizetype numShadows = 0;
    std::array<std::array<quint8, NUM_TEXTURE_SIZES>, 2> textureSizeLayerCount = {}; // 0: 16 bit, 1: 32bit
    QVarLengthArray<quint8, 16> lightIndexToLayerStartIndex;
    lightIndexToLayerStartIndex.resize(numLights * MAX_SPLITS);

    // NOTE: This is a quite ugly workaround. If 32bit is not supported then go through all lights and disable it.
    const bool supports32BitTextures = rhi->isTextureFormatSupported(QRhiTexture::R32F);
    if (!supports32BitTextures) {
        bool any32bit = false;
        for (quint32 lightIndex = 0; lightIndex < numLights; ++lightIndex) {
            QSSGRenderLight *light = renderableLights.at(lightIndex).light;
            any32bit = any32bit || light->m_use32BitShadowmap;
            light->m_use32BitShadowmap = false;
        }
        static bool warned32bit = false;
        if (!warned32bit && any32bit) {
            qWarning() << "WARN: 32 bit shadow maps are unsupported, falling back to 16 bit.";
            warned32bit = true;
        }
    }

    for (quint32 lightIndex = 0; lightIndex < numLights; ++lightIndex) {
        const QSSGShaderLight &shaderLight = renderableLights.at(lightIndex);
        ShadowMapModes mapMode = (shaderLight.light->type == QSSGRenderLight::Type::PointLight) ? ShadowMapModes::CUBE
                                                                                                : ShadowMapModes::VSM;
        if (shaderLight.shadows)
            numShadows += 1;
        if (!shaderLight.shadows || mapMode == ShadowMapModes::CUBE)
            continue;

        quint32 mapSize = shaderLight.light->m_shadowMapRes;
        quint8 &layerCount = textureSizeLayerCount[shaderLight.light->m_use32BitShadowmap ? 1 : 0][mapSizeToIndex(mapSize)];
        quint8 layerIndex = layerCount;
        layerCount += shaderLight.light->m_csmNumSplits + 1;
        lightIndexToLayerStartIndex[lightIndex] = layerIndex;
    }

    // Only recreate shadow assets if something has changed
    bool needsRebuild = numShadows != shadowMapEntryCount();
    if (!needsRebuild) {
        // Check if relevant shadow properties has changed
        for (quint32 lightIndex = 0; lightIndex < numLights; ++lightIndex) {
            const QSSGShaderLight &shaderLight = renderableLights.at(lightIndex);
            if (!shaderLight.shadows)
                continue;

            QSSGShadowMapEntry *pEntry = shadowMapEntry(lightIndex);
            if (!pEntry) {
                needsRebuild = true;
                break;
            }

            QRhiTexture::Format textureFormat = getShadowMapTextureFormat(rhi, shaderLight.light->m_use32BitShadowmap);
            ShadowMapModes mapMode = (shaderLight.light->type == QSSGRenderLight::Type::PointLight) ? ShadowMapModes::CUBE
                                                                                                    : ShadowMapModes::VSM;
            quint32 mapSize = shaderLight.light->m_shadowMapRes;
            quint32 csmNumSplits = shaderLight.light->m_csmNumSplits;
            quint32 layerIndex = mapMode == ShadowMapModes::VSM ? lightIndexToLayerStartIndex[lightIndex] : 0;
            if (!pEntry->isCompatible(QSize(mapSize, mapSize), layerIndex, csmNumSplits, mapMode, textureFormat)) {
                needsRebuild = true;
                break;
            }
        }
    }

    if (!needsRebuild)
        return;

    releaseCachedResources();

    // Create VSM texture arrays
    for (quint32 hashI = 0; hashI < 2; ++hashI) {
        const bool use32bit = hashI == 1;
        QRhiTexture::Format rhiFormat = getShadowMapTextureFormat(rhi, use32bit);
        for (quint32 sizeI = 0; sizeI < NUM_TEXTURE_SIZES; sizeI++) {
            const quint32 numLayers = textureSizeLayerCount[hashI][sizeI];
            if (numLayers == 0)
                continue;

            const quint32 mapSize = indexToMapSize(sizeI);
            QSize texSize = QSize(mapSize, mapSize);
            auto texture = allocateRhiShadowTexture(rhi, rhiFormat, texSize, numLayers, QRhiTexture::RenderTarget | QRhiTexture::TextureArray);
            m_depthTextureArrays[hashI].insert(texSize, texture);
        }
    }

    // Setup render targets
    for (quint32 lightIdx = 0; lightIdx < numLights; ++lightIdx) {
        const auto &shaderLight = renderableLights.at(lightIdx);
        if (!shaderLight.shadows)
            continue;

        const bool use32bit = shaderLight.light->m_use32BitShadowmap;
        QSize mapSize = QSize(shaderLight.light->m_shadowMapRes, shaderLight.light->m_shadowMapRes);
        ShadowMapModes mapMode = (shaderLight.light->type == QSSGRenderLight::Type::PointLight) ? ShadowMapModes::CUBE
                                                                                                : ShadowMapModes::VSM;
        switch (mapMode) {
        case ShadowMapModes::VSM: {
            quint32 layerStartIndex = lightIndexToLayerStartIndex.value(lightIdx);
            quint32 csmNumSplits = shaderLight.light->m_csmNumSplits;
            addDirectionalShadowMap(lightIdx, mapSize, use32bit, layerStartIndex, csmNumSplits, shaderLight.light->debugObjectName);
            break;
        }
        case ShadowMapModes::CUBE: {
            addCubeShadowMap(lightIdx, mapSize, use32bit, shaderLight.light->debugObjectName);
            break;
        }
        default:
            Q_UNREACHABLE();
            break;
        }
    }
}

QSSGShadowMapEntry *QSSGRenderShadowMap::addDirectionalShadowMap(qint32 lightIdx,
                                                                 QSize size,
                                                                 bool use32bit,
                                                                 quint32 layerStartIndex,
                                                                 quint32 csmNumSplits,
                                                                 const QString &renderNodeObjName)
{
    QRhi *rhi = m_context.rhiContext()->rhi();
    QSSGShadowMapEntry *pEntry = shadowMapEntry(lightIdx);

    Q_ASSERT(rhi);
    Q_ASSERT(!pEntry);

    auto texture = m_depthTextureArrays[use32bit ? 1 : 0].value(size);
    Q_ASSERT(texture);
    m_shadowMapList.push_back(QSSGShadowMapEntry::withRhiDepthMap(lightIdx, ShadowMapModes::VSM, texture));

    pEntry = &m_shadowMapList.back();
    pEntry->m_csmNumSplits = csmNumSplits;

    // Additional graphics resources: samplers, render targets.
    for (quint32 splitIndex = 0; splitIndex < csmNumSplits + 1; splitIndex++) {
        QRhiTextureRenderTarget *&rt(pEntry->m_rhiRenderTargets[splitIndex]);
        Q_ASSERT(!rt);

        pEntry->m_rhiDepthStencil[splitIndex] = allocateRhiShadowRenderBuffer(rhi, QRhiRenderBuffer::DepthStencil, size);

        QRhiTextureRenderTargetDescription rtDesc;
        QRhiColorAttachment attachment(pEntry->m_rhiDepthTextureArray);
        attachment.setLayer(layerStartIndex + splitIndex);
        rtDesc.setColorAttachments({ attachment });
        rtDesc.setDepthStencilBuffer(pEntry->m_rhiDepthStencil[splitIndex]);
        rt = rhi->newTextureRenderTarget(rtDesc);
        rt->setDescription(rtDesc);
        // The same renderpass descriptor can be reused since the
        // format, load/store ops are the same regardless of the shadow mode.
        if (!pEntry->m_rhiRenderPassDesc[splitIndex])
            pEntry->m_rhiRenderPassDesc[splitIndex] = rt->newCompatibleRenderPassDescriptor();
        rt->setRenderPassDescriptor(pEntry->m_rhiRenderPassDesc[splitIndex]);
        if (!rt->create())
            qWarning("Failed to build shadow map render target");

        const QByteArray rtName = renderNodeObjName.toLatin1();
        rt->setName(rtName + QByteArrayLiteral(" shadow map"));
    }

    pEntry->m_lightIndex = lightIdx;
    pEntry->m_depthArrayIndex = layerStartIndex;

    return pEntry;
}

QSSGShadowMapEntry *QSSGRenderShadowMap::addCubeShadowMap(qint32 lightIdx, QSize size, bool use32bit, const QString &renderNodeObjName)
{
    QRhi *rhi = m_context.rhiContext()->rhi();
    QSSGShadowMapEntry *pEntry = shadowMapEntry(lightIdx);

    Q_ASSERT(rhi);
    Q_ASSERT(!pEntry);

    QRhiTexture::Format rhiFormat = getShadowMapTextureFormat(rhi, use32bit);
    QRhiTexture *depthMap = allocateRhiShadowTexture(rhi, rhiFormat, size, 0, QRhiTexture::RenderTarget | QRhiTexture::CubeMap);
    QRhiRenderBuffer *depthStencil = allocateRhiShadowRenderBuffer(rhi, QRhiRenderBuffer::DepthStencil, size);
    m_shadowMapList.push_back(QSSGShadowMapEntry::withRhiDepthCubeMap(lightIdx, ShadowMapModes::CUBE, depthMap, depthStencil));
    pEntry = &m_shadowMapList.back();

    const QByteArray rtName = renderNodeObjName.toLatin1();

    for (const auto face : QSSGRenderTextureCubeFaces) {
        QRhiTextureRenderTarget *&rt(pEntry->m_rhiRenderTargets[quint8(face)]);
        Q_ASSERT(!rt);
        QRhiColorAttachment att(pEntry->m_rhiDepthCube);
        att.setLayer(quint8(face)); // 6 render targets, each referencing one face of the cubemap
        QRhiTextureRenderTargetDescription rtDesc;
        rtDesc.setColorAttachments({ att });
        rtDesc.setDepthStencilBuffer(pEntry->m_rhiDepthStencil[0]);
        rt = rhi->newTextureRenderTarget(rtDesc);
        rt->setDescription(rtDesc);
        if (!pEntry->m_rhiRenderPassDesc[0])
            pEntry->m_rhiRenderPassDesc[0] = rt->newCompatibleRenderPassDescriptor();
        rt->setRenderPassDescriptor(pEntry->m_rhiRenderPassDesc[0]);
        if (!rt->create())
            qWarning("Failed to build shadow map render target");
        rt->setName(rtName + QByteArrayLiteral(" shadow cube face: ") + QSSGBaseTypeHelpers::displayName(face));
    }

    return pEntry;
}

QSSGShadowMapEntry *QSSGRenderShadowMap::shadowMapEntry(int lightIdx)
{
    Q_ASSERT(lightIdx >= 0);

    for (int i = 0; i < m_shadowMapList.size(); i++) {
        QSSGShadowMapEntry *pEntry = &m_shadowMapList[i];
        if (pEntry->m_lightIndex == quint32(lightIdx))
            return pEntry;
    }

    return nullptr;
}

QSSGShadowMapEntry::QSSGShadowMapEntry()
    : m_lightIndex(std::numeric_limits<quint32>::max())
    , m_shadowMapMode(ShadowMapModes::VSM)
{
}

QSSGShadowMapEntry QSSGShadowMapEntry::withRhiDepthMap(quint32 lightIdx, ShadowMapModes mode, QRhiTexture *textureArray)
{
    QSSGShadowMapEntry e;
    e.m_lightIndex = lightIdx;
    e.m_shadowMapMode = mode;
    e.m_rhiDepthTextureArray = textureArray;
    return e;
}

QSSGShadowMapEntry QSSGShadowMapEntry::withRhiDepthCubeMap(quint32 lightIdx, ShadowMapModes mode, QRhiTexture *depthCube, QRhiRenderBuffer *depthStencil)
{
    QSSGShadowMapEntry e;
    e.m_lightIndex = lightIdx;
    e.m_shadowMapMode = mode;
    e.m_rhiDepthCube = depthCube;
    e.m_rhiDepthStencil[0] = depthStencil;
    return e;
}

bool QSSGShadowMapEntry::isCompatible(QSize mapSize, quint32 layerIndex, quint32 csmNumSplits, ShadowMapModes mapMode, QRhiTexture::Format textureFormat)
{
    if (csmNumSplits != m_csmNumSplits)
        return false;

    if (mapMode != m_shadowMapMode)
        return false;

    switch (mapMode) {
    case ShadowMapModes::CUBE: {
        if (mapSize != m_rhiDepthCube->pixelSize()) {
            return false;
        }
        break;
    }
    case ShadowMapModes::VSM: {
        if (mapSize != m_rhiDepthTextureArray->pixelSize() || int(layerIndex) >= m_rhiDepthTextureArray->arraySize()
            || textureFormat != m_rhiDepthTextureArray->format()) {
            return false;
        }
        break;
    }
    default:
        Q_UNREACHABLE();
        break;
    }

    return true;
}

void QSSGShadowMapEntry::destroyRhiResources()
{
    m_rhiDepthTextureArray = nullptr;

    delete m_rhiDepthCube;
    m_rhiDepthCube = nullptr;
    qDeleteAll(m_rhiDepthStencil);
    m_rhiDepthStencil.fill(nullptr);
    qDeleteAll(m_rhiRenderTargets);
    m_rhiRenderTargets.fill(nullptr);
    qDeleteAll(m_rhiRenderPassDesc);
    m_rhiRenderPassDesc.fill(nullptr);
}

QT_END_NAMESPACE
