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
  function buildVisualHull(silhouettes, viewAngles) {
    // 포인트 수를 균일하게 맞추기 위해 가장 적은 수로 리샘플링
    const minLen = Math.min(...silhouettes.map(s => s.length));
    const resampled = silhouettes.map(sil => resamplePoints(sil, minLen));

    const vertices = [];
    const faces = [];
    const viewOffsets = []; // 각 뷰의 시작 인덱스

    // 1. 각 뷰의 실루엣 포인트를 3D 공간에 배치
    for (let i = 0; i < resampled.length; i++) {
      const sil = resampled[i];
      const angle = viewAngles[i];
      const rad = angle * Math.PI / 180;
      const cos = Math.cos(rad);
      const sin = Math.sin(rad);

      viewOffsets.push(vertices.length);
      for (const p of sil) {
        const wx = p.x * cos - p.z * sin;
        const wy = p.y;
        const wz = p.x * sin + p.z * cos;
        vertices.push({ x: wx, y: wy, z: wz });
      }
    }

    // 2. 인접 뷰 간의 포인트를 연결하여 측면 메시 생성
    // 모든 뷰가 동일한 포인트 수를 가지므로 1:1 연결 가능
    for (let i = 0; i < resampled.length - 1; i++) {
      const lenA = resampled[i].length;
      const lenB = resampled[i + 1].length;
      const offsetA = viewOffsets[i];
      const offsetB = viewOffsets[i + 1];
      const segCount = Math.max(lenA, lenB);

      for (let j = 0; j < segCount; j++) {
        const idxA = offsetA + (j % lenA);
        const idxB = offsetB + (j % lenB);
        const idxA2 = offsetA + ((j + 1) % lenA);
        const idxB2 = offsetB + ((j + 1) % lenB);
        // 두 개의 삼각형으로 쿼드 구성
        faces.push([idxA, idxB, idxB2]);
        faces.push([idxA, idxB2, idxA2]);
      }
    }

    // 3. 양 끝 뷰에 캡 추가 (fan triangulation)
    // 첫 번째 뷰 (front)
    const firstOffset = viewOffsets[0];
    const firstLen = resampled[0].length;
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

    // 마지막 뷰 (back)
    const lastOffset = viewOffsets[viewOffsets.length - 1];
    const lastLen = resampled[resampled.length - 1].length;
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