// viewModes.js — 뷰모드 레지스트리 (standard/cel/dummy)

(function(global) {
  'use strict';

  // ── 뷰모드 레지스트리 ──
  const viewModes = [];
  let currentMode = 'standard';

  function registerViewMode(mode) {
    // mode: { id, name, label, shader, description }
    if (viewModes.find(m => m.id === mode.id)) return; // 중복 등록 방지
    viewModes.push(mode);
  }

  function getViewMode(id) {
    return viewModes.find(m => m.id === id) || viewModes[0];
  }

  function getCurrentMode() {
    return getViewMode(currentMode);
  }

  function setCurrentMode(id) {
    if (viewModes.find(m => m.id === id)) {
      currentMode = id;
    }
  }

  function getAllModes() {
    return viewModes.slice();
  }

  // ── 기본 뷰모드 등록 ──
  function registerDefaultModes() {
    registerViewMode({
      id: 'standard',
      name: 'standard',
      label: '일반 3D',
      shader: 'pbr',
      description: '일반 3D (PBR/standard)'
    });
    registerViewMode({
      id: 'cel',
      name: 'cel',
      label: '셀 셰이딩',
      shader: 'cel',
      description: '길티기어식 셀 셰이딩 (2~3톤 밴드+림라이트+외곽선)'
    });
    registerViewMode({
      id: 'dummy',
      name: 'dummy',
      label: '더미',
      shader: 'dummy',
      description: '단색 더미 렌더'
    });
  }

  // ── 토글 UI 생성 ──
  function createToggleUI(container, labelEl, onChange) {
    container.innerHTML = '';
    for (const mode of viewModes) {
      const btn = document.createElement('button');
      btn.textContent = mode.label;
      btn.dataset.modeId = mode.id;
      if (mode.id === currentMode) btn.classList.add('active');
      btn.addEventListener('click', () => {
        setCurrentMode(mode.id);
        container.querySelectorAll('button').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        if (labelEl) labelEl.textContent = mode.label;
        if (onChange) onChange(mode.id);
      });
      container.appendChild(btn);
    }
  }

  // ── 내보내기 ──
  global.SketchViewModes = {
    registerViewMode,
    getViewMode,
    getCurrentMode,
    setCurrentMode,
    getAllModes,
    registerDefaultModes,
    createToggleUI
  };

})(typeof window !== 'undefined' ? window : globalThis);
