// guides.js — 프리셋 가이드 메쉬 생성 (입체/구/원기둥/원뿔/원환/원환둥/목업인형)

(function(global) {
  'use strict';

  // ── 큐브 ──
  function createCubeGuide(size) {
    const s = size / 2;
    const vertices = [
      {x:-s,y:-s,z:-s},{x:s,y:-s,z:-s},{x:s,y:s,z:-s},{x:-s,y:s,z:-s},
      {x:-s,y:-s,z:s},{x:s,y:-s,z:s},{x:s,y:s,z:s},{x:-s,y:s,z:s}
    ];
    const faces = [
      [0,1,2,3],[4,5,6,7],[0,1,5,4],[2,3,7,6],[0,3,7,4],[1,2,6,5]
    ];
    return { vertices, faces };
  }

  // ── 구 ──
  function createSphereGuide(radius, segments) {
    segments = segments || 12;
    const vertices = [];
    const faces = [];
    for (let lat = 0; lat <= segments; lat++) {
      const theta = lat * Math.PI / segments;
      const sinT = Math.sin(theta);
      const cosT = Math.cos(theta);
      for (let lon = 0; lon <= segments; lon++) {
        const phi = lon * 2 * Math.PI / segments;
        vertices.push({
          x: radius * sinT * Math.cos(phi),
          y: radius * cosT,
          z: radius * sinT * Math.sin(phi)
        });
      }
    }
    for (let lat = 0; lat < segments; lat++) {
      for (let lon = 0; lon < segments; lon++) {
        const a = lat * (segments + 1) + lon;
        const b = a + segments + 1;
        faces.push([a, b, b + 1, a + 1]);
      }
    }
    return { vertices, faces };
  }

  // ── 원기둥 ──
  function createCylinderGuide(radius, height, segments) {
    segments = segments || 12;
    const vertices = [];
    const faces = [];
    const h = height / 2;
    for (let i = 0; i <= segments; i++) {
      const angle = i * 2 * Math.PI / segments;
      const x = radius * Math.cos(angle);
      const z = radius * Math.sin(angle);
      vertices.push({x, y:-h, z});
      vertices.push({x, y:h, z});
    }
    for (let i = 0; i < segments; i++) {
      const a = i * 2;
      faces.push([a, a + 2, a + 3, a + 1]);
    }
    // 상단/하단 캡
    const topCenter = vertices.length;
    vertices.push({x:0, y:h, z:0});
    const botCenter = vertices.length;
    vertices.push({x:0, y:-h, z:0});
    for (let i = 0; i < segments; i++) {
      const a = i * 2;
      faces.push([topCenter, a + 1, a + 3]);
      faces.push([botCenter, a + 2, a]);
    }
    return { vertices, faces };
  }

  // ── 원뿔 ──
  function createConeGuide(radius, height, segments) {
    segments = segments || 12;
    const vertices = [];
    const faces = [];
    const h = height / 2;
    for (let i = 0; i <= segments; i++) {
      const angle = i * 2 * Math.PI / segments;
      vertices.push({x: radius * Math.cos(angle), y:-h, z: radius * Math.sin(angle)});
    }
    const apex = vertices.length;
    vertices.push({x:0, y:h, z:0});
    for (let i = 0; i < segments; i++) {
      faces.push([apex, i, (i + 1) % segments]);
    }
    const botCenter = vertices.length;
    vertices.push({x:0, y:-h, z:0});
    for (let i = 0; i < segments; i++) {
      faces.push([botCenter, (i + 1) % segments, i]);
    }
    return { vertices, faces };
  }

  // ── 원환 ──
  function createTorusGuide(R, r, majorSeg, minorSeg) {
    majorSeg = majorSeg || 16;
    minorSeg = minorSeg || 8;
    const vertices = [];
    const faces = [];
    for (let i = 0; i <= majorSeg; i++) {
      const u = i * 2 * Math.PI / majorSeg;
      const cosU = Math.cos(u);
      const sinU = Math.sin(u);
      for (let j = 0; j <= minorSeg; j++) {
        const v = j * 2 * Math.PI / minorSeg;
        const cosV = Math.cos(v);
        const sinV = Math.sin(v);
        vertices.push({
          x: (R + r * cosV) * cosU,
          y: r * sinV,
          z: (R + r * cosV) * sinU
        });
      }
    }
    for (let i = 0; i < majorSeg; i++) {
      for (let j = 0; j < minorSeg; j++) {
        const a = i * (minorSeg + 1) + j;
        const b = a + minorSeg + 1;
        faces.push([a, b, b + 1, a + 1]);
      }
    }
    return { vertices, faces };
  }

  // ── 원환둥 (구형 토러스) ──
  function createTorus2Guide(R, r, majorSeg, minorSeg) {
    return createTorusGuide(R, r, majorSeg || 20, minorSeg || 10);
  }

  // ── 목업 인형 ──
  // 성별(남성/여성) × 나이(성인/소년/아동(남)/아동(여))
  function createMannequinGuide(gender, age) {
    const isMale = gender === 'male';
    const isChild = age === 'child';
    const isBoy = age === 'boy';
    const isGirl = age === 'girl';
    const isYouth = age === 'youth';

    // 신체 비율 (키 기준)
    let headR, torsoW, torsoH, limbW, limbL, shoulderW, hipW;
    if (isChild) {
      headR = 0.12; torsoW = 0.18; torsoH = 0.25; limbW = 0.05; limbL = 0.35;
      shoulderW = 0.22; hipW = 0.16;
    } else if (isYouth || isBoy || isGirl) {
      headR = 0.10; torsoW = 0.20; torsoH = 0.35; limbW = 0.06; limbL = 0.45;
      shoulderW = 0.28; hipW = 0.20;
    } else {
      headR = 0.09; torsoW = isMale ? 0.24 : 0.20; torsoH = 0.45;
      limbW = isMale ? 0.07 : 0.06; limbL = 0.55;
      shoulderW = isMale ? 0.32 : 0.26; hipW = isMale ? 0.22 : 0.24;
    }

    const vertices = [];
    const faces = [];

    // 머리 (구)
    const headCenter = { x: 0, y: torsoH + headR * 1.5, z: 0 };
    const headSegs = 8;
    for (let lat = 0; lat <= headSegs; lat++) {
      const theta = lat * Math.PI / headSegs;
      for (let lon = 0; lon <= headSegs; lon++) {
        const phi = lon * 2 * Math.PI / headSegs;
        vertices.push({
          x: headCenter.x + headR * Math.sin(theta) * Math.cos(phi),
          y: headCenter.y + headR * Math.cos(theta),
          z: headCenter.z + headR * Math.sin(theta) * Math.sin(phi)
        });
      }
    }
    for (let lat = 0; lat < headSegs; lat++) {
      for (let lon = 0; lon < headSegs; lon++) {
        const a = lat * (headSegs + 1) + lon;
        const b = a + headSegs + 1;
        faces.push([a, b, b + 1, a + 1]);
      }
    }

    // 몸통 (상단 박스)
    const torsoY = torsoH / 2;
    const torsoVerts = [
      {x:-torsoW/2, y:0, z:-torsoW/2},{x:torsoW/2, y:0, z:-torsoW/2},
      {x:torsoW/2, y:torsoH, z:-torsoW/2},{x:-torsoW/2, y:torsoH, z:-torsoW/2},
      {x:-torsoW/2, y:0, z:torsoW/2},{x:torsoW/2, y:0, z:torsoW/2},
      {x:torsoW/2, y:torsoH, z:torsoW/2},{x:-torsoW/2, y:torsoH, z:torsoW/2}
    ];
    const torsoBase = vertices.length;
    for (const v of torsoVerts) vertices.push({x:v.x, y:v.y + torsoY, z:v.z});
    faces.push(
      [torsoBase+0, torsoBase+1, torsoBase+2, torsoBase+3],
      [torsoBase+4, torsoBase+5, torsoBase+6, torsoBase+7],
      [torsoBase+0, torsoBase+1, torsoBase+5, torsoBase+4],
      [torsoBase+2, torsoBase+3, torsoBase+7, torsoBase+6],
      [torsoBase+0, torsoBase+3, torsoBase+7, torsoBase+4],
      [torsoBase+1, torsoBase+2, torsoBase+6, torsoBase+5]
    );

    // 팔 (4개 세그먼트)
    const armY = torsoH * 0.85;
    for (let side = -1; side <= 1; side += 2) {
      const armX = side * (torsoW / 2 + limbW / 2);
      const armVerts = [
        {x:armX - limbW/2, y:armY, z:-limbW/2},
        {x:armX + limbW/2, y:armY, z:-limbW/2},
        {x:armX + limbW/2, y:armY - limbL, z:-limbW/2},
        {x:armX - limbW/2, y:armY - limbL, z:-limbW/2},
        {x:armX - limbW/2, y:armY, z:limbW/2},
        {x:armX + limbW/2, y:armY, z:limbW/2},
        {x:armX + limbW/2, y:armY - limbL, z:limbW/2},
        {x:armX - limbW/2, y:armY - limbL, z:limbW/2}
      ];
      const armBase = vertices.length;
      for (const v of armVerts) vertices.push(v);
      faces.push(
        [armBase+0, armBase+1, armBase+2, armBase+3],
        [armBase+4, armBase+5, armBase+6, armBase+7],
        [armBase+0, armBase+1, armBase+5, armBase+4],
        [armBase+2, armBase+3, armBase+7, armBase+6],
        [armBase+0, armBase+3, armBase+7, armBase+4],
        [armBase+1, armBase+2, armBase+6, armBase+5]
      );
    }

    // 다리
    const legY = 0;
    for (let side = -1; side <= 1; side += 2) {
      const legX = side * (hipW / 4);
      const legVerts = [
        {x:legX - limbW/2, y:legY, z:-limbW/2},
        {x:legX + limbW/2, y:legY, z:-limbW/2},
        {x:legX + limbW/2, y:legY - limbL, z:-limbW/2},
        {x:legX - limbW/2, y:legY - limbL, z:-limbW/2},
        {x:legX - limbW/2, y:legY, z:limbW/2},
        {x:legX + limbW/2, y:legY, z:limbW/2},
        {x:legX + limbW/2, y:legY - limbL, z:limbW/2},
        {x:legX - limbW/2, y:legY - limbL, z:limbW/2}
      ];
      const legBase = vertices.length;
      for (const v of legVerts) vertices.push(v);
      faces.push(
        [legBase+0, legBase+1, legBase+2, legBase+3],
        [legBase+4, legBase+5, legBase+6, legBase+7],
        [legBase+0, legBase+1, legBase+5, legBase+4],
        [legBase+2, legBase+3, legBase+7, legBase+6],
        [legBase+0, legBase+3, legBase+7, legBase+4],
        [legBase+1, legBase+2, legBase+6, legBase+5]
      );
    }

    return { vertices, faces };
  }

  // ── 가이드 선택 ──
  function getGuide(name) {
    switch (name) {
      case 'cube': return createCubeGuide(100);
      case 'sphere': return createSphereGuide(50, 12);
      case 'cylinder': return createCylinderGuide(30, 100, 12);
      case 'cone': return createConeGuide(40, 100, 12);
      case 'torus': return createTorusGuide(40, 15, 16, 8);
      case 'torus2': return createTorus2Guide(40, 15, 20, 10);
      case 'mannequin-male': return createMannequinGuide('male', 'adult');
      case 'mannequin-female': return createMannequinGuide('female', 'adult');
      case 'mannequin-adult': return createMannequinGuide('male', 'adult');
      case 'mannequin-youth': return createMannequinGuide('male', 'youth');
      case 'mannequin-boy': return createMannequinGuide('male', 'boy');
      case 'mannequin-girl': return createMannequinGuide('female', 'girl');
      case 'mannequin-child-m': return createMannequinGuide('male', 'child');
      case 'mannequin-child-f': return createMannequinGuide('female', 'child');
      default: return null;
    }
  }

  // ── 내보내기 ──
  global.SketchGuides = {
    createCubeGuide,
    createSphereGuide,
    createCylinderGuide,
    createConeGuide,
    createTorusGuide,
    createTorus2Guide,
    createMannequinGuide,
    getGuide
  };

})(typeof window !== 'undefined' ? window : globalThis);
