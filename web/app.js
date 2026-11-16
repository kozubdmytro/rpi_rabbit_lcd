'use strict';
const $ = id => document.getElementById(id);
const states = ['Ожидание', 'В очереди', 'Добавляем ноду…', 'Настраиваем ноду…', 'Сбрасываем ключи ноды…', 'Удаляем запись и ключ на хабе…', 'Готово', 'Ошибка'];
const errors = ['', 'Ошибка радиоканала', 'Не удалось добавить ноду', 'Не удалось настроить ноду', 'Истекло время ожидания', 'Нода не подтвердила сброс ключей. Включите её и повторите удаление.', 'Не удалось удалить ключ на хабе. Повторите удаление.', 'Не удалось сохранить удаление. Повторите удаление.', 'Поиск остановлен'];
let requestId = crypto.getRandomValues(new Uint16Array(1))[0] || 1;
let online = false, pending = false, snapshot = null, trackedId = 0;
let brightnessDirty = false, rotationDirty = false;
let menuDraft = null, menuDirty = false, menuNodes = '';
function message(text, error = false) { $('message').textContent = text; $('message').classList.toggle('error', error); }
function controls() {
  document.querySelectorAll('button,input,select').forEach(element => { element.disabled = !online || pending; });
  const busy = snapshot && Boolean(snapshot.operation.flags & 64);
  $('scan').disabled = !online || pending || busy || Boolean(snapshot?.operation.scanSeconds);
  document.querySelectorAll('.node button').forEach(button => { button.disabled = !online || pending || busy; });
  if (snapshot?.nodes.length >= 10) document.querySelectorAll('#candidates button').forEach(element => { element.disabled = true; });
}
async function post(path, fields) {
  if (pending) return;
  pending = true; controls();
  try {
    const id = requestId = requestId % 65535 + 1;
    const response = await fetch(path, { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded', 'X-SR-Request': '1' }, body: new URLSearchParams(['/api/display', '/api/menu'].includes(path) ? fields : { ...fields, id }), signal: AbortSignal.timeout(5000) });
    const result = await response.json();
    if (!response.ok) throw new Error(({128: 'Хаб занят. Дождитесь окончания операции.', 129: 'Нода не найдена. Обновите страницу.', 131: 'Mesh ещё не готов.', 132: 'Конфликт команды. Повторите действие.'})[result.code] || result.error || 'Команда отклонена');
    if (!['/api/display', '/api/menu'].includes(path)) trackedId = id;
    message(['/api/display', '/api/menu'].includes(path) ? 'Настройка сохранена.' : 'Команда принята. Ожидаем результат…');
    return true;
  } catch (error) { message(`Не удалось выполнить команду: ${error.message}`, true); return false; }
  finally { pending = false; controls(); }
}
$('brightness').addEventListener('input', () => { brightnessDirty = true; $('brightness-value').value = `${$('brightness').value}%`; });
$('rotation').addEventListener('change', () => { rotationDirty = true; });
$('brightness-form').addEventListener('submit', async event => { event.preventDefault(); if (await post('/api/display', { brightness: $('brightness').value })) brightnessDirty = false; });
$('rotation-form').addEventListener('submit', async event => { event.preventDefault(); if (await post('/api/display', { rotation: $('rotation').value })) rotationDirty = false; });
$('scan').addEventListener('click', () => post('/api/scan', {}));
function button(label, action, danger = false) {
  const element = document.createElement('button'); element.type = 'button'; element.textContent = label;
  if (danger) element.className = 'danger'; element.addEventListener('click', action); return element;
}
function row(title, subtitle) {
  const item = document.createElement('li'); item.className = 'node';
  const content = document.createElement('div'), name = document.createElement('strong'), detail = document.createElement('p'), actions = document.createElement('div');
  name.textContent = title; detail.textContent = subtitle; actions.className = 'actions'; content.append(name, detail); item.append(content, actions);
  return { item, actions };
}
function render(state) {
  snapshot = state; online = state.ready;
  $('connection').textContent = online ? 'Хаб подключён' : 'Хаб запускается…'; $('connection').classList.toggle('online', online);
  $('simulation').hidden = !state.simulation;
  $('address').textContent = state.wifi.ip ? `http://${state.wifi.ip}:${location.port || 80}` : 'Wi-Fi отключён · Bluetooth доступен для настройки';
  if (!brightnessDirty && !pending) { $('brightness').value = state.brightness; $('brightness-value').value = `${state.brightness}%`; }
  if (!rotationDirty && !pending) $('rotation').value = state.rotation;
  $('node-count').textContent = `${state.nodes.length} / 10`;
  const operation = state.operation;
  $('operation').textContent = operation.scanSeconds ? `Идёт поиск · ещё ${operation.scanSeconds} с` : states[operation.state] || 'Неизвестное состояние';
  if (trackedId && operation.id === trackedId && operation.state >= 6) {
    message(operation.state === 7 ? errors[operation.error] || 'Операция завершилась с ошибкой' : operation.flags & 32 ? 'Запись удалена с хаба. Для повторного подключения сбросьте саму ноду.' : 'Операция завершена.', operation.state === 7); trackedId = 0;
  }
  const nodes = state.nodes.map(node => {
    const address = `0x${node.address.toString(16).padStart(4, '0')}`;
    const type = node.type === 1 ? 'Температура / влажность' : node.type === 2 ? 'Влажность почвы' : 'Сенсорная нода';
    const status = node.removing ? 'Удаление не завершено' : node.fresh ? (node.configured ? 'На связи' : 'Данные приходят · настройка не подтверждена') : !node.configured ? 'Настройка не подтверждена' : node.fresh ? 'На связи' : 'Ожидаем данные';
    const { item, actions } = row(`${type} · ${address}`, `${status}${node.fresh && node.temperature != null ? ` · ${node.temperature.toFixed(1)} °C / ${node.humidity.toFixed(1)}%` : ''} · ${node.uuid}`);
    if (!node.configured && !node.removing) actions.append(button('Настроить', () => post('/api/retry', { address: node.address })));
    actions.append(button('Удалить', () => {
      if (confirm(`Удалить ноду ${address}? Ключи будут сброшены на ноде и удалены с хаба. Нода должна быть включена.`)) post('/api/remove', { address: node.address });
    }, true));
    actions.append(button('Забыть на хабе', () => {
      if (confirm(`Забыть ${address} только на хабе? Это удалит локальную запись и ключ даже без ответа ноды. Сама нода сохранит старую сеть: для повторного добавления потребуется её аппаратный сброс.`)) post('/api/forget', { address: node.address });
    }, true));
    return item;
  });
  $('nodes').replaceChildren(...nodes); $('empty').hidden = Boolean(nodes.length);
  $('discovery').hidden = !state.candidates.length;
  $('candidates').replaceChildren(...state.candidates.map(uuid => {
    const { item, actions } = row('Новая нода', uuid);
    const add = button('Добавить', () => post('/api/bind', { uuid })); actions.append(add); return item;
  }));
  if (!menuDirty && !pending) {
    const entries = state.menu || [];
    const nodes = JSON.stringify(state.nodes.map(n => [n.address, n.type, n.removing]));
    if (JSON.stringify(menuDraft) !== JSON.stringify(entries) || menuNodes !== nodes) {menuDraft = structuredClone(entries); menuNodes = nodes; renderMenu();}
  }
  controls();
}
const menuTypes = ['climate', 'soil', 'lights', 'plug', 'blinds', 'thermo'];
function renderMenu() {
  $('menu-items').replaceChildren(...(menuDraft || []).map((entry, index) => {
    const { item, actions } = row(`${index + 1}. ${menuTypes[entry.type]}`, '');
    const name = document.createElement('input'); name.value = entry.name; name.maxLength = 16; name.setAttribute('aria-label', 'Имя пункта');
    name.addEventListener('input', () => { entry.name = name.value; menuDirty = true; });
    item.firstChild.append(name);
    if (entry.type === 0) {
      const select = document.createElement('select'); select.setAttribute('aria-label', 'Нода для climate');
      const options = [{ address: 0, label: 'Нода не назначена' }, ...(snapshot?.nodes || []).filter(n => n.type === 1 && !n.removing).map(n => ({ address: n.address, label: `0x${n.address.toString(16)}${n.fresh ? ' · на связи' : ''}` }))];
      if (entry.address && !options.some(n => n.address === entry.address)) options.push({ address: entry.address, label: `0x${entry.address.toString(16)} · недоступна` });
      for (const node of options) { const option = document.createElement('option'); option.value = node.address; option.textContent = node.label; select.append(option); }
      select.value = entry.address; select.addEventListener('change', () => { entry.address = Number(select.value); menuDirty = true; }); item.firstChild.append(select);
    }
    for (const [label, direction] of [['↑', -1], ['↓', 1]]) actions.append(button(label, () => {
      const other = index + direction; if (other < 0 || other >= menuDraft.length) return;
      [menuDraft[index], menuDraft[other]] = [menuDraft[other], menuDraft[index]]; menuDirty = true; renderMenu(); controls();
    }));
    actions.append(button('Убрать пункт', () => {menuDraft.splice(index, 1); menuDirty = true; renderMenu(); controls();}));
    return item;
  }));
}
$('menu-add').addEventListener('click', () => {
  if (!menuDraft || menuDraft.length >= 16) return;
  const type = Number($('menu-type').value); menuDraft.push({ type, name: menuTypes[type], address: 0 }); menuDirty = true; renderMenu(); controls();
});
$('menu-cancel').addEventListener('click', () => {menuDraft = structuredClone(snapshot.menu || []); menuDirty = false; renderMenu(); controls();});
$('menu-save').addEventListener('click', async () => {
  if (!menuDraft || menuDraft.some(entry => !/^[a-zA-Z0-9 _.-]{1,16}$/.test(entry.name))) {message('Имя: от 1 до 16 символов латиницы, цифр, пробела, _ - .', true); return;}
  const fields = { count: menuDraft.length };
  menuDraft.forEach((entry, i) => {fields[`type${i}`] = entry.type; fields[`name${i}`] = entry.name; fields[`address${i}`] = entry.address;});
  if (await post('/api/menu', fields)) menuDirty = false;
});
async function refresh() {
  try {
    const response = await fetch('/api/state', { cache: 'no-store', signal: AbortSignal.timeout(5000) });
    if (!response.ok) throw new Error('Хаб не отвечает');
    const state = await response.json(); render(state);
    if ($('message').textContent === 'Загружаем состояние хаба…' || $('message').dataset.disconnected) { message('Можно управлять хабом.'); delete $('message').dataset.disconnected; }
  } catch (error) {
    online = false; controls(); $('connection').textContent = 'Нет связи'; $('connection').classList.remove('online');
    message('Связь с хабом потеряна. Переподключаемся…', true); $('message').dataset.disconnected = '1';
  } finally { setTimeout(refresh, 1500); }
}
controls(); refresh();
