// mesh.js — M12: 실루엣 포인트 추출 + Visual Hull 메시 생성 + watertight 패치 + 오버레이 판정
(function(global) {
  'use strict';

  const Core = global.SketchCore;

  // ═══════════════════════════════════════════
  // 실루엣 포인트 추출 (4뷰 → 3D)
  // ═══════════════════════════════════════════

  // 실루엣 포인트를 월드 좌표로 변환 (카메라 투영 역변환)
  function extractPointsFromSilhouettes(silhouettes, viewAngles) {
    const allPoints = [];
    for (let i = 0; i < silhouettes.length; i++) {
      const sil = silhouettes[i];
      const angle = viewAngles[i];
      const rad = angle * Math.PI / 180;
      const cos = Math.cos(rad);
      const sin = Math.sin(rad);
      for (const p of sil) {
        // 카메라 좌표 → 월드 좌표
        const wx = p.x * cos - p.z * sin;
        const wy = p.y;
        const wz = p.x * sin + p.z * cos;
        allPoints.push({ x: wx, y: wy, z: wz });
      }
    }
    return allPoints;
  }

  // ═══════════════════════════════════════════
  // Visual Hull 메시 생성
  // ═══════════════════════════════════════════

  // 포인트를 균일하게 리샘플링 (인접 뷰 간 포인트 수 불일치 해결)
  function resamplePoints(points, targetCount) {
    if (points.length === targetCount) return points.slice();
    const result = [];
    for (let i = 0; i < targetCount; i++) {
      const idx = Math.floor(i * points.length / targetCount);
      result.push(points[idx]);
    }
    return result;
  }

  // 실루엣 포인트들을 연결하여 3D 메시 생성
  // - 각 뷰의 실루엣을 3D 공간에 배치
  // - 인접 뷰 간의 포인트를 연결하여 측면 메시 생성
  // - 양 끝 뷰에 캡 추가 (fan triangulation)
  function buildVisualHull(silhouettes, viewAngles, options = {}) {
    const subdivisions = options.subdivisions || 18; // 뷰 간 중간 레이어 수

    // 포인트 수를 균일하게 맞추기 위해 가장 적은 수로 리샘플링
    const minLen = Math.min(...silhouettes.map(s => s.length));
    const resampled = silhouettes.map(sil => resamplePoints(sil, minLen));

    // Step 1: 각 뷰의 실루엣을 중심점 기준 각도 정렬
    const sortedSilhouettes = resampled.map((sil, viewIdx) => {
      const angle = viewAngles[viewIdx];
      const rad = angle * Math.PI / 180;
      const cos = Math.cos(rad), sin = Math.sin(rad);

      // 월드 좌표 변환
      const worldPts = sil.map(p => ({
        x: p.x * cos - p.z * sin,
        y: p.y,
        z: p.x * sin + p.z * cos
      }));

      // 중심점 계산
      let cx = 0, cy = 0, cz = 0;
      for (const p of worldPts) { cx += p.x; cy += p.y; cz += p.z; }
      cx /= worldPts.length; cy /= worldPts.length; cz /= worldPts.length;

      // 실루엣 평면에서 각도 정렬
      const bx = cos, bz = sin;
      const sorted = worldPts.map((p) => {
        const dx = p.x - cx, dy = p.y - cy, dz = p.z - cz;
        const u = dx * bx + dz * bz;
        const v = dy;
        return { angle: Math.atan2(v, u), point: p };
      }).sort((a, b) => a.angle - b.angle);

      return sorted.map(s => s.point);
    });

    // Step 2: 인접 뷰 간 최적 회전 오프셋 탐색 (엣지 길이 최소화)
    const n = sortedSilhouettes[0].length;
    const rotations = [0];

    for (let i = 1; i < sortedSilhouettes.length; i++) {
      const prev = sortedSilhouettes[i - 1];
      const curr = sortedSilhouettes[i];

      let bestOffset = 0;
      let bestCost = Infinity;

      for (let offset = 0; offset < n; offset++) {
        let cost = 0;
        for (let j = 0; j < n; j++) {
          const p1 = prev[j];
          const p2 = curr[(j + offset) % n];
          const dx = p1.x - p2.x, dy = p1.y - p2.y, dz = p1.z - p2.z;
          cost += dx * dx + dy * dy + dz * dz;
        }
        if (cost < bestCost) {
          bestCost = cost;
          bestOffset = offset;
        }
      }

      rotations.push(bestOffset);
    }

    // Step 3: 뷰 간 중간 레이어 보간 (메시 밀집화)
    // 각 뷰와 중간 레이어를 하나의 "레이어 스택"으로 구성
    const layers = []; // 각 레이어는 n개의 3D 포인트

    for (let i = 0; i < sortedSilhouettes.length; i++) {
      const sil = sortedSilhouettes[i];
      const offset = rotations[i];

      // 현재 뷰의 레이어 추가
      const currentLayer = [];
      for (let j = 0; j < sil.length; j++) {
        const idx = (j + offset) % sil.length;
        currentLayer.push(sil[idx]);
      }
      layers.push(currentLayer);

      // 다음 뷰와의 중간 레이어 추가
      if (i < sortedSilhouettes.length - 1) {
        const nextSil = sortedSilhouettes[i + 1];
        const nextOffset = rotations[i + 1];

        for (let k = 1; k <= subdivisions; k++) {
          const t = k / (subdivisions + 1);
          const midLayer = [];
          for (let j = 0; j < n; j++) {
            const idxA = (j + offset) % n;
            const idxB = (j + nextOffset) % n;
            const pA = sil[idxA];
            const pB = nextSil[idxB];
            midLayer.push({
              x: pA.x + (pB.x - pA.x) * t,
              y: pA.y + (pB.y - pA.y) * t,
              z: pA.z + (pB.z - pA.z) * t
            });
          }
          layers.push(midLayer);
        }
      }
    }

    // Step 4: 레이어 스택으로 메시 구성
    const vertices = [];
    const faces = [];
    const layerOffsets = [];

    for (let i = 0; i < layers.length; i++) {
      layerOffsets.push(vertices.length);
      for (const p of layers[i]) {
        vertices.push(p);
      }
    }

    // Step 5: 인접 레이어 간 연결
    for (let i = 0; i < layers.length - 1; i++) {
      const lenA = layers[i].length;
      const lenB = layers[i + 1].length;
      const offsetA = layerOffsets[i];
      const offsetB = layerOffsets[i + 1];
      const segCount = Math.max(lenA, lenB);

      for (let j = 0; j < segCount; j++) {
        const idxA = offsetA + (j % lenA);
        const idxB = offsetB + (j % lenB);
        const idxA2 = offsetA + ((j + 1) % lenA);
        const idxB2 = offsetB + ((j + 1) % lenB);
        faces.push([idxA, idxB, idxB2]);
        faces.push([idxA, idxB2, idxA2]);
      }
    }

    // Step 6: 양 끝 레이어에 캡 추가 (fan triangulation)
    const firstOffset = layerOffsets[0];
    const firstLen = layers[0].length;
    const firstCenter = vertices.length;
    let firstCx = 0, firstCy = 0, firstCz = 0;
    for (let i = 0; i < firstLen; i++) {
      firstCx += vertices[firstOffset + i].x;
      firstCy += vertices[firstOffset + i].y;
      firstCz += vertices[firstOffset + i].z;
    }
    firstCx /= firstLen;
    firstCy /= firstLen;
    firstCz /= firstLen;
    vertices.push({ x: firstCx, y: firstCy, z: firstCz });
    for (let i = 0; i < firstLen; i++) {
      const next = (i + 1) % firstLen;
      faces.push([firstCenter, firstOffset + i, firstOffset + next]);
    }

    const lastOffset = layerOffsets[layerOffsets.length - 1];
    const lastLen = layers[layers.length - 1].length;
    const lastCenter = vertices.length;
    let lastCx = 0, lastCy = 0, lastCz = 0;
    for (let i = 0; i < lastLen; i++) {
      lastCx += vertices[lastOffset + i].x;
      lastCy += vertices[lastOffset + i].y;
      lastCz += vertices[lastOffset + i].z;
    }
    lastCx /= lastLen;
    lastCy /= lastLen;
    lastCz /= lastLen;
    vertices.push({ x: lastCx, y: lastCy, z: lastCz });
    for (let i = 0; i < lastLen; i++) {
      const next = (i + 1) % lastLen;
      faces.push([lastCenter, lastOffset + next, lastOffset + i]);
    }

    return { vertices, faces };
  }

  // ═══════════════════════════════════════════
  // Watertight 패치 (hole filling)
  // ═══════════════════════════════════════════

  // 메시의 hole을 감지하고 채움
  // hole = 경계 엣지(1번만 등장하는 엣지)로 이루어진 닫힌 루프
  function buildWatertightCaps(mesh) {
    const edgeCount = new Map();
    const edgeVertices = new Map();

    // 엣지 카운팅
    for (const face of mesh.faces) {
      const n = face.length;
      for (let i = 0; i < n; i++) {
        const a = face[i];
        const b = face[(i + 1) % n];
        const key = a < b ? `${a}_${b}` : `${b}_${a}`;
        if (!edgeCount.has(key)) {
          edgeCount.set(key, 0);
          edgeVertices.set(key, [a, b]);
        }
        edgeCount.set(key, edgeCount.get(key) + 1);
      }
    }

    // 경계 엣지 추출 (1번만 등장)
    const boundaryEdges = [];
    for (const [key, count] of edgeCount) {
      if (count === 1) {
        boundaryEdges.push(edgeVertices.get(key));
      }
    }

    // 경계 엣지를 연결하여 hole 루프 찾기
    const adj = new Map();
    for (const [a, b] of boundaryEdges) {
      if (!adj.has(a)) adj.set(a, []);
      if (!adj.has(b)) adj.set(b, []);
      adj.get(a).push(b);
      adj.get(b).push(a);
    }

    // DFS로 닫힌 루프 찾기
    const visited = new Set();
    const holes = [];
    for (const start of adj.keys()) {
      if (visited.has(start)) continue;
      const hole = [];
      const stack = [start];
      while (stack.length > 0) {
        const curr = stack.pop();
        if (visited.has(curr)) continue;
        visited.add(curr);
        hole.push(curr);
        for (const next of adj.get(curr) || []) {
          if (!visited.has(next)) stack.push(next);
        }
      }
      if (hole.length >= 3) holes.push(hole);
    }

    // 각 hole을 fan triangulation으로 채우기
    const newVertices = mesh.vertices.slice();
    const newFaces = mesh.faces.slice();
    for (const hole of holes) {
      // hole의 중심점 계산
      let cx = 0, cy = 0, cz = 0;
      for (const idx of hole) {
        cx += newVertices[idx].x;
        cy += newVertices[idx].y;
        cz += newVertices[idx].z;
      }
      cx /= hole.length;
      cy /= hole.length;
      cz /= hole.length;
      const centerIdx = newVertices.length;
      newVertices.push({ x: cx, y: cy, z: cz });
      // fan triangulation
      for (let i = 0; i < hole.length; i++) {
        const next = (i + 1) % hole.length;
        newFaces.push([centerIdx, hole[i], hole[next]]);
      }
    }

    return { vertices: newVertices, faces: newFaces };
  }

  // ═══════════════════════════════════════════
  // 점이 visual hull 내부인지 판정 (Ray Casting)
  // ═══════════════════════════════════════════

  // 점이 메시 내부에 있는지 판정 (even-odd rule)
  function isInsideVisualHull(point, mesh) {
    // 메시의 바운딩 박스 계산
    let minX = Infinity, maxX = -Infinity;
    let minY = Infinity, maxY = -Infinity;
    let minZ = Infinity, maxZ = -Infinity;
    for (const v of mesh.vertices) {
      if (v.x < minX) minX = v.x;
      if (v.x > maxX) maxX = v.x;
      if (v.y < minY) minY = v.y;
      if (v.y > maxY) maxY = v.y;
      if (v.z < minZ) minZ = v.z;
      if (v.z > maxZ) maxZ = v.z;
    }
    // 바운딩 박스 밖이면 외부
    if (point.x < minX || point.x > maxX ||
        point.y < minY || point.y > maxY ||
        point.z < minZ || point.z > maxZ) {
      return false;
    }

    // X축 방향 레이 캐스팅
    const rayDir = { x: 1, y: 0, z: 0 };
    let intersections = 0;
    for (const face of mesh.faces) {
      if (rayIntersectsTriangle(point, rayDir, face, mesh.vertices)) {
        intersections++;
      }
    }
    return intersections % 2 === 1;
  }

  // 레이-삼각형 교차 판정
  function rayIntersectsTriangle(origin, dir, face, vertices) {
    const v0 = vertices[face[0]];
    const v1 = vertices[face[1]];
    const v2 = vertices[face[2]];
    const epsilon = 0.0001;

    const edge1 = { x: v1.x - v0.x, y: v1.y - v0.y, z: v1.z - v0.z };
    const edge2 = { x: v2.x - v0.x, y: v2.y - v0.y, z: v2.z - v0.z };
    const h = cross(dir, edge2);
    const a = dot(edge1, h);
    if (a > -epsilon && a < epsilon) return false;
    const f = 1 / a;
    const s = { x: origin.x - v0.x, y: origin.y - v0.y, z: origin.z - v0.z };
    const u = f * dot(s, h);
    if (u < 0 || u > 1) return false;
    const q = cross(s, edge1);
    const v = f * dot(dir, q);
    if (v < 0 || u + v > 1) return false;
    const t = f * dot(edge2, q);
    return t > epsilon;
  }

  function cross(a, b) {
    return {
      x: a.y * b.z - a.z * b.y,
      y: a.z * b.x - a.x * b.z,
      z: a.x * b.y - a.y * b.x
    };
  }

  function dot(a, b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
  }

  // ═══════════════════════════════════════════
  // 오버레이 판정 (두 메시의 교차 영역)
  // ═══════════════════════════════════════════

  // 두 메시의 오버레이 영역을 계산
  // - 메시 A의 각 면이 메시 B 내부에 있는지 판정
  // - 메시 B의 각 면이 메시 A 내부에 있는지 판정
  function checkOverlay(meshA, meshB) {
    const overlayFacesA = [];
    const overlayFacesB = [];

    for (let i = 0; i < meshA.faces.length; i++) {
      const face = meshA.faces[i];
      // 면의 중심점
      let cx = 0, cy = 0, cz = 0;
      for (const idx of face) {
        cx += meshA.vertices[idx].x;
        cy += meshA.vertices[idx].y;
        cz += meshA.vertices[idx].z;
      }
      cx /= face.length;
      cy /= face.length;
      cz /= face.length;
      if (isInsideVisualHull({ x: cx, y: cy, z: cz }, meshB)) {
        overlayFacesA.push(i);
      }
    }

    for (let i = 0; i < meshB.faces.length; i++) {
      const face = meshB.faces[i];
      let cx = 0, cy = 0, cz = 0;
      for (const idx of face) {
        cx += meshB.vertices[idx].x;
        cy += meshB.vertices[idx].y;
        cz += meshB.vertices[idx].z;
      }
      cx /= face.length;
      cy /= face.length;
      cz /= face.length;
      if (isInsideVisualHull({ x: cx, y: cy, z: cz }, meshA)) {
        overlayFacesB.push(i);
      }
    }

    return {
      meshAOverlayCount: overlayFacesA.length,
      meshBOverlayCount: overlayFacesB.length,
      meshAOverlayFaces: overlayFacesA,
      meshBOverlayFaces: overlayFacesB,
      overlayRatioA: overlayFacesA.length / meshA.faces.length,
      overlayRatioB: overlayFacesB.length / meshB.faces.length
    };
  }

  // ═══════════════════════════════════════════
  // 4방향 렌더링 데이터 생성
  // ═══════════════════════════════════════════

  // 메시를 4방향(front/side/top/3-4)으로 렌더링하기 위한 데이터 생성
  function prepareRenderData(mesh, viewAngles) {
    const renderData = [];
    for (const angle of viewAngles) {
      const rad = angle * Math.PI / 180;
      const cos = Math.cos(rad);
      const sin = Math.sin(rad);
      const camVertices = mesh.vertices.map(v => ({
        x: v.x * cos + v.z * sin,
        y: v.y,
        z: -v.x * sin + v.z * cos
      }));
      // 깊이 정렬
      const facesWithDepth = mesh.faces.map((face, idx) => {
        let zSum = 0;
        for (const vi of face) zSum += camVertices[vi].z;
        return { face, idx, avgZ: zSum / face.length };
      }).sort((a, b) => a.avgZ - b.avgZ);
      renderData.push({
        angle,
        camVertices,
        sortedFaces: facesWithDepth
      });
    }
    return renderData;
  }

  // ═══════════════════════════════════════════
  // 내보내기
  // ═══════════════════════════════════════════
  global.SketchMesh = {
    extractPointsFromSilhouettes,
    buildVisualHull,
    buildWatertightCaps,
    isInsideVisualHull,
    checkOverlay,
    prepareRenderData
  };

})(typeof window !== 'undefined' ? window : globalThis);