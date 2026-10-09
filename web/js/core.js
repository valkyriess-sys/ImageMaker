// core.js — 좌표계, 데이터 모델, 변환 파이프라인, 부분 병합

(function(global) {
  'use strict';

  // ── 각도 정규화 ──
  function normalizeAngle(angle) {
    angle = angle % 360;
    if (angle > 180) angle -= 360;
    if (angle < -180) angle += 360;
    return angle;
  }

  // ── 좌표 변환 ──
  // 월드 → 카메라 (Y축 회전)
  function worldToCamera(worldX, worldY, worldZ, viewAngleDeg) {
    const rad = viewAngleDeg * Math.PI / 180;
    const cos = Math.cos(rad);
    const sin = Math.sin(rad);
    return {
      x: worldX * cos + worldZ * sin,
      y: worldY,
      z: -worldX * sin + worldZ * cos
    };
  }

  // 카메라 → 월드 (역투영, z=0 가정)
  function cameraToWorld(cameraX, cameraY, cameraZ, viewAngleDeg) {
    const rad = viewAngleDeg * Math.PI / 180;
    const cos = Math.cos(rad);
    const sin = Math.sin(rad);
    return {
      x: cameraX * cos - cameraZ * sin,
      y: cameraY,
      z: cameraX * sin + cameraZ * cos
    };
  }

  // 스크린 → 월드 (z=0 평면 가정)
  function screenToWorld(screenX, screenY, viewAngleDeg, viewport, canvasWidth, canvasHeight) {
    const cameraX = (screenX - canvasWidth / 2) / viewport.scale + viewport.centerX;
    const cameraY = -(screenY - canvasHeight / 2) / viewport.scale + viewport.centerY;
    return cameraToWorld(cameraX, cameraY, 0, viewAngleDeg);
  }

  // 월드 → 스크린
  function worldToScreen(worldX, worldY, worldZ, viewAngleDeg, viewport, canvasWidth, canvasHeight) {
    const cam = worldToCamera(worldX, worldY, worldZ, viewAngleDeg);
    return {
      x: (cam.x - viewport.centerX) * viewport.scale + canvasWidth / 2,
      y: -(cam.y - viewport.centerY) * viewport.scale + canvasHeight / 2
    };
  }

  // ── 브러시 정의 ──
  const BRUSHES = {
    pencil: { baseWidth: 0.8, round: true,  opacity: 0.9,  pressureToWidth: 0.02 },
    pen:    { baseWidth: 1.5, round: true,  opacity: 1.0,  pressureToWidth: 0.04 },
    marker: { baseWidth: 3.0, round: true,  opacity: 0.7,  pressureToWidth: 0.08 },
    brush:  { baseWidth: 4.5, round: false, opacity: 0.85, pressureToWidth: 0.12 }
  };

  function getBrush(name) {
    return BRUSHES[name] || BRUSHES.pen;
  }

  // ── 스트로크 모델 ──
  let strokeIdCounter = 0;

  function createStroke(points3D, viewAngle, viewport, pressure) {
    return {
      id: 'stroke_' + (++strokeIdCounter),
      points3D: points3D.map(p => ({ x: p.x, y: p.y, z: p.z })),
      viewAngle: viewAngle,
      viewport: {
        centerX: viewport.centerX,
        centerY: viewport.centerY,
        scale: viewport.scale
      },
      pressure: pressure ? pressure.slice() : null,
      timestamp: Date.now()
    };
  }

  // ── 대칭 프록시 생성 (X 반전 + viewAngle 부호) ──
  function createSymmetricProxy(stroke) {
    return {
      id: stroke.id + '_sym',
      points3D: stroke.points3D.map(p => ({ x: -p.x, y: p.y, z: p.z })),
      viewAngle: -stroke.viewAngle,
      viewport: {
        centerX: stroke.viewport.centerX,
        centerY: stroke.viewport.centerY,
        scale: stroke.viewport.scale
      },
      pressure: stroke.pressure ? stroke.pressure.slice() : null,
      timestamp: stroke.timestamp
    };
  }

  // ── 부분 병합 (Partial Merge) ──
  function falloff(dist, radius) {
    const t = 1 - (dist / radius);
    return t * t * (3 - 2 * t);
  }

  function distance3D(a, b) {
    const dx = a.x - b.x;
    const dy = a.y - b.y;
    const dz = a.z - b.z;
    return Math.sqrt(dx * dx + dy * dy + dz * dz);
  }

  function partialMerge(vertices, sourceStrokes, targetRegion, mergeMode) {
    const affected = [];
    for (let i = 0; i < vertices.length; i++) {
      const v = vertices[i];
      const dist = distance3D(v, targetRegion.anchorPoint);
      if (dist <= targetRegion.radius) {
        const factor = falloff(dist, targetRegion.radius);
        let sourceAvg = { x: 0, y: 0, z: 0 };
        for (const s of sourceStrokes) {
          for (const p of s.points3D) {
            sourceAvg.x += p.x;
            sourceAvg.y += p.y;
            sourceAvg.z += p.z;
          }
        }
        const total = sourceStrokes.reduce((sum, s) => sum + s.points3D.length, 0);
        if (total > 0) {
          sourceAvg.x /= total;
          sourceAvg.y /= total;
          sourceAvg.z /= total;
        }
        if (mergeMode === 'add') {
          v.x += sourceAvg.x * factor;
          v.y += sourceAvg.y * factor;
          v.z += sourceAvg.z * factor;
        } else if (mergeMode === 'replace') {
          v.x = sourceAvg.x;
          v.y = sourceAvg.y;
          v.z = sourceAvg.z;
        } else if (mergeMode === 'blend') {
          v.x = v.x + (sourceAvg.x - v.x) * factor;
          v.y = v.y + (sourceAvg.y - v.y) * factor;
          v.z = v.z + (sourceAvg.z - v.z) * factor;
        }
        affected.push(i);
      }
    }
    return affected;
  }

  // ── Onion-Skin 렌더 ──
  function renderOnionSkin(stroke, currentAngle, currentViewport, canvasWidth, canvasHeight) {
    const angleDiff = Math.abs(normalizeAngle(stroke.viewAngle - currentAngle));
    if (angleDiff >= 30) return null;
    const opacity = 1.0 - (angleDiff / 30.0) * 0.7;
    const screenPoints = stroke.points3D.map(p => {
      return worldToScreen(p.x, p.y, p.z, currentAngle, currentViewport, canvasWidth, canvasHeight);
    });
    return { screenPoints, opacity };
  }

  // ── 저장/복원 ──
  function serializeState(state) {
    return JSON.stringify({
      strokes: state.strokes,
      currentAngle: state.currentAngle,
      viewport: state.viewport,
      onionSkinEnabled: state.onionSkinEnabled
    });
  }

  function deserializeState(json) {
    const data = JSON.parse(json);
    return {
      strokes: data.strokes || [],
      currentAngle: data.currentAngle || 0,
      viewport: data.viewport || { centerX: 0, centerY: 0, scale: 1 },
      onionSkinEnabled: data.onionSkinEnabled !== false
    };
  }

  // ── ViewState 생성 ──
  function createViewState() {
    return {
      strokes: [],
      currentAngle: 0,
      viewport: { centerX: 0, centerY: 0, scale: 1 },
      onionSkinEnabled: true
    };
  }

  // ── 내보내기 ──
  global.SketchCore = {
    normalizeAngle,
    worldToCamera,
    cameraToWorld,
    screenToWorld,
    worldToScreen,
    BRUSHES,
    getBrush,
    createStroke,
    createSymmetricProxy,
    falloff,
    distance3D,
    partialMerge,
    renderOnionSkin,
    serializeState,
    deserializeState,
    createViewState
  };

})(typeof window !== 'undefined' ? window : globalThis);
