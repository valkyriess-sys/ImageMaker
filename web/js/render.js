// render.js — 렌더링 파이프라인 (standard/cel/dummy 뷰모드)

(function(global) {
  'use strict';

  const Core = global.SketchCore;

  // ── 배경 그리기 ──
  function drawBackground(ctx, width, height) {
    ctx.fillStyle = '#1a1a2e';
    ctx.fillRect(0, 0, width, height);
  }

  // ── 스트로크 그리기 (브러시 너비/pressure 반영) ──
  function drawPolyline(ctx, points, brush, pressureArray) {
    if (points.length < 2) return;
    ctx.save();
    ctx.lineCap = brush.round ? 'round' : 'butt';
    ctx.lineJoin = 'round';
    ctx.globalAlpha = brush.opacity;

    for (let i = 0; i < points.length - 1; i++) {
      const p1 = points[i];
      const p2 = points[i + 1];
      let width = brush.baseWidth;
      if (pressureArray && pressureArray[i] !== undefined) {
        width = brush.baseWidth + pressureArray[i] * brush.pressureToWidth * 10;
      }
      ctx.lineWidth = width;
      ctx.beginPath();
      ctx.moveTo(p1.x, p1.y);
      ctx.lineTo(p2.x, p2.y);
      ctx.stroke();
    }
    ctx.restore();
  }

  // ── 스트로크 렌더 (단일) ──
  function drawStroke(ctx, stroke, currentAngle, currentViewport, canvasWidth, canvasHeight, brush) {
    const screenPoints = stroke.points3D.map(p => {
      return Core.worldToScreen(p.x, p.y, p.z, currentAngle, currentViewport, canvasWidth, canvasHeight);
    });
    ctx.strokeStyle = '#4a90d9';
    drawPolyline(ctx, screenPoints, brush, stroke.pressure);
  }

  // ── Onion-skin 렌더 ──
  function drawOnionSkin(ctx, stroke, currentAngle, currentViewport, canvasWidth, canvasHeight, brush) {
    const result = Core.renderOnionSkin(stroke, currentAngle, currentViewport, canvasWidth, canvasHeight);
    if (!result) return;
    ctx.save();
    ctx.globalAlpha = result.opacity;
    ctx.strokeStyle = '#6a6a8a';
    ctx.lineWidth = brush.baseWidth * 0.7;
    ctx.setLineDash([4, 4]);
    ctx.beginPath();
    for (let i = 0; i < result.screenPoints.length; i++) {
      const p = result.screenPoints[i];
      if (i === 0) ctx.moveTo(p.x, p.y);
      else ctx.lineTo(p.x, p.y);
    }
    ctx.stroke();
    ctx.setLineDash([]);
    ctx.restore();
  }

  // ── 가이드 렌더 ──
  function drawGuide(ctx, guide, currentAngle, currentViewport, canvasWidth, canvasHeight) {
    if (!guide || !guide.vertices || !guide.faces) return;
    ctx.save();
    ctx.strokeStyle = '#3a3a5a';
    ctx.lineWidth = 0.5;
    ctx.globalAlpha = 0.4;

    // 외곽선 추출 (3~5개 겹친 선 → 대표 외곽선 1개)
    const edgeCount = {};
    for (const face of guide.faces) {
      for (let i = 0; i < face.length; i++) {
        const a = face[i];
        const b = face[(i + 1) % face.length];
        const key = a < b ? a + '_' + b : b + '_' + a;
        edgeCount[key] = (edgeCount[key] || 0) + 1;
      }
    }
    // 외곽선 = 1번만 등장하는 엣지
    const outlineEdges = Object.keys(edgeCount).filter(k => edgeCount[k] === 1);
    for (const key of outlineEdges) {
      const [a, b] = key.split('_').map(Number);
      const pa = Core.worldToScreen(guide.vertices[a].x, guide.vertices[a].y, guide.vertices[a].z, currentAngle, currentViewport, canvasWidth, canvasHeight);
      const pb = Core.worldToScreen(guide.vertices[b].x, guide.vertices[b].y, guide.vertices[b].z, currentAngle, currentViewport, canvasWidth, canvasHeight);
      ctx.beginPath();
      ctx.moveTo(pa.x, pa.y);
      ctx.lineTo(pb.x, pb.y);
      ctx.stroke();
    }
    ctx.restore();
  }

  // ── 셰이딩 렌더 (cel/dummy) ──
  function drawShadedMesh(ctx, mesh, currentAngle, currentViewport, canvasWidth, canvasHeight, mode) {
    if (!mesh.vertices || !mesh.faces) return;
    ctx.save();

    // 월드 → 카메라 변환
    const camPoints = mesh.vertices.map(v => {
      return Core.worldToCamera(v.x, v.y, v.z, currentAngle);
    });

    // 깊이 정렬 (painter's algorithm)
    const facesWithDepth = mesh.faces.map((face, idx) => {
      let zSum = 0;
      for (const vi of face) zSum += camPoints[vi].z;
      return { face, idx, avgZ: zSum / face.length };
    }).sort((a, b) => a.avgZ - b.avgZ);

    for (const { face, idx } of facesWithDepth) {
      // 법선 계산 (카메라 좌표계)
      const p0 = camPoints[face[0]];
      const p1 = camPoints[face[1]];
      const p2 = camPoints[face[2]];
      const ux = p1.x - p0.x, uy = p1.y - p0.y, uz = p1.z - p0.z;
      const vx = p2.x - p0.x, vy = p2.y - p0.y, vz = p2.z - p0.z;
      let nx = uy * vz - uz * vy;
      let ny = uz * vx - ux * vz;
      let nz = ux * vy - uy * vx;
      const len = Math.sqrt(nx * nx + ny * ny + nz * nz);
      if (len > 0) { nx /= len; ny /= len; nz /= len; }

      // 조명 (카메라 방향에서 비춤)
      const lightDir = { x: 0, y: 0, z: 1 };
      const dot = nx * lightDir.x + ny * lightDir.y + nz * lightDir.z;
      const intensity = Math.max(0, dot);

      // 색상 결정
      let r, g, b, a;
      if (mode === 'cel') {
        // 2~3톤 밴드
        const tone = intensity > 0.7 ? 1.0 : intensity > 0.4 ? 0.65 : 0.35;
        const base = mesh.faceColors && mesh.faceColors[idx] ? mesh.faceColors[idx] : { r: 0.4, g: 0.6, b: 0.9, a: 1 };
        r = base.r * tone * 255;
        g = base.g * tone * 255;
        b = base.b * tone * 255;
        a = 1;
      } else {
        // dummy (단색)
        const base = mesh.faceColors && mesh.faceColors[idx] ? mesh.faceColors[idx] : { r: 0.5, g: 0.5, b: 0.5, a: 1 };
        r = base.r * 255;
        g = base.g * 255;
        b = base.b * 255;
        a = base.a;
      }

      // 스크린 좌표로 변환
      const screenPts = face.map(vi => {
        const cam = camPoints[vi];
        return {
          x: (cam.x - currentViewport.centerX) * currentViewport.scale + canvasWidth / 2,
          y: -(cam.y - currentViewport.centerY) * currentViewport.scale + canvasHeight / 2
        };
      });

      ctx.fillStyle = `rgba(${r|0},${g|0},${b|0},${a})`;
      ctx.beginPath();
      ctx.moveTo(screenPts[0].x, screenPts[0].y);
      for (let i = 1; i < screenPts.length; i++) {
        ctx.lineTo(screenPts[i].x, screenPts[i].y);
      }
      ctx.closePath();
      ctx.fill();

      // 외곽선 (cel 모드)
      if (mode === 'cel') {
        ctx.strokeStyle = 'rgba(20,20,40,0.8)';
        ctx.lineWidth = 1;
        ctx.stroke();
      }
    }
    ctx.restore();
  }

  // ── 전체 렌더 ──
  function render(ctx, state, guide, brush, viewMode, canvasWidth, canvasHeight) {
    drawBackground(ctx, canvasWidth, canvasHeight);

    // 가이드
    if (guide) {
      drawGuide(ctx, guide, state.currentAngle, state.viewport, canvasWidth, canvasHeight);
    }

    // Onion-skin
    if (state.onionSkinEnabled) {
      for (const stroke of state.strokes) {
        if (stroke.viewAngle !== state.currentAngle) {
          drawOnionSkin(ctx, stroke, state.currentAngle, state.viewport, canvasWidth, canvasHeight, brush);
        }
      }
    }

    // 현재 스트로크
    for (const stroke of state.strokes) {
      if (stroke.viewAngle === state.currentAngle) {
        drawStroke(ctx, stroke, state.currentAngle, state.viewport, canvasWidth, canvasHeight, brush);
      }
    }

    // 셰이딩 메쉬 (cel/dummy 모드)
    if (viewMode === 'cel' || viewMode === 'dummy') {
      if (guide && guide.vertices && guide.faces) {
        drawShadedMesh(ctx, guide, state.currentAngle, state.viewport, canvasWidth, canvasHeight, viewMode);
      }
    }
  }

  // ── 내보내기 ──
  global.SketchRender = {
    drawBackground,
    drawPolyline,
    drawStroke,
    drawOnionSkin,
    drawGuide,
    drawShadedMesh,
    render
  };

})(typeof window !== 'undefined' ? window : globalThis);
