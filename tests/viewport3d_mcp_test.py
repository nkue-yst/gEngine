"""実Qt Editorの3D入力とMCPを交互に実行し、GPU画像・履歴・取消し・PIEを検証する。"""
import base64
import ctypes
from ctypes import wintypes
import json
import math
from pathlib import Path
import sys
import time
import uuid
from editor_mcp_test import Client


def run(executable, workspace, hold=False):
    workspace.mkdir(parents=True, exist_ok=True)
    client = Client(executable, workspace)
    user = ctypes.windll.user32
    callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.EnumWindows.argtypes = [callback, wintypes.LPARAM]
    user.EnumChildWindows.argtypes = [wintypes.HWND, callback, wintypes.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
    user.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.UINT]
    user.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]

    def mutate(name, args):
        return client.tool(name, args, uuid.uuid4().hex)

    def state():
        return client.tool('document.get_state')

    def target():
        s = state()
        return dict(document_id=s['document_id'], expected_revision=s['revision'])

    def view_state():
        return client.tool('viewport.get_state', target())

    def objects():
        return {o['kind']: o for o in state()['scene']['objects']}

    def handles():
        parents = []
        children = []
        @callback
        def top(hwnd, _):
            pid = wintypes.DWORD()
            user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            name = ctypes.create_unicode_buffer(256)
            user.GetClassNameW(hwnd, name, 256)
            if pid.value == client.process.pid and name.value.startswith('Qt') and 'QWindow' in name.value:
                parents.append(hwnd)
            return True
        @callback
        def child(hwnd, _):
            text = ctypes.create_unicode_buffer(256)
            user.GetWindowTextW(hwnd, text, 256)
            if text.value == 'gEngine GameRunner':
                children.append(hwnd)
            return True
        user.EnumWindows(top, 0)
        assert len(parents) == 1, parents
        user.EnumChildWindows(parents[0], child, 0)
        assert len(children) == 1, children
        return parents[0], children[0]

    def projection(world, size=None):
        s = view_state()
        rect = wintypes.RECT()
        user.GetClientRect(view, ctypes.byref(rect))
        width, height = size or (rect.right, rect.bottom)
        yaw, pitch = math.radians(s['yaw_degrees']), math.radians(s['pitch_degrees'])
        right = [math.cos(yaw), 0, -math.sin(yaw)]
        up = [-math.sin(yaw)*math.sin(pitch), math.cos(pitch), -math.cos(yaw)*math.sin(pitch)]
        unit = min(width/16, height/9)*9/s['height']
        relative = [world[i]-s['center'][i] for i in range(3)]
        center = [width/2+sum(relative[i]*right[i] for i in range(3))*unit,
                  height/2-sum(relative[i]*up[i] for i in range(3))*unit]
        return center, [[right[i]*unit, -up[i]*unit] for i in range(3)]

    def pixel(world):
        return tuple(round(n) for n in projection(world)[0])

    def mouse(message, point, flags=0):
        x, y = (round(n) for n in point)
        user.SendMessageW(view, message, flags, (x & 65535) | ((y & 65535) << 16))

    def click(point):
        mouse(0x201, point, 1)
        mouse(0x202, point)

    def drag_start(axis):
        position = objects()['player_spawn']['position']
        click(pixel(position))
        assert view_state()['selected_id'] == 'player'
        center, axes = projection(position)
        direction = axes[axis]
        length = math.hypot(*direction)
        start = [center[i]+44*direction[i]/length for i in range(2)]
        mouse(0x201, start, 1)
        assert view_state()['transient'], (axis, start, view_state())
        return start, direction

    def capture(name):
        result = client.request('tools/call', dict(name='viewport.capture', arguments=target()))
        assert result.get('isError') is False, result
        data = base64.b64decode(result['content'][1]['data'])
        (workspace/name).write_bytes(data)
        return data

    def undo():
        mutate('history.undo', dict(**target(), history_id=state()['undo_id']))

    try:
        client.initialize()
        project = mutate('project.create', dict(path=uuid.uuid4().hex, template_name='3d'))['project']
        mutate('document.open', dict(path='scene.json'))
        parent, view = handles()
        mutate('viewport.set_view', dict(**target(), center=[0,2,0], height=20, yaw_degrees=45, pitch_degrees=30))
        capture('initial-3d.png')
        click(pixel([0,2,4]))
        capture('selected-3d.png')
        # 色・位置の画素照合はQt Testの既存PNGデコーダーで行う。
        for axis in range(3):
            before = state()
            start, direction = drag_start(axis)
            if axis == 0:
                # 左ドラッグ中に右ボタンを押しても、その解放で確定しない。
                mouse(0x204, start, 3)
                mouse(0x205, start, 1)
                assert view_state()['transient'] and state()['revision'] == before['revision']
            moved = [start[i]+direction[i] for i in range(2)]
            mouse(0x200, moved, 1)
            assert state()['revision'] == before['revision']
            assert client.tool('viewport.capture', target(), expect_error=True)['error']['code'] == 'Busy'
            mouse(0x202, moved)
            after = state()
            position = objects()['player_spawn']['position']
            assert after['revision'] == before['revision']+1
            expected = [0,2,4]
            expected[axis] += 1
            assert all(abs(a-b)<.15 for a,b in zip(position,expected)), (axis,position)
            capture(f'moved-axis-{axis}.png')
            undo()
            assert objects()['player_spawn']['position'] == [0,2,4]
        # 連続編集でも同じネイティブ描画ウィンドウを維持する。
        for iteration in range(24):
            start, direction = drag_start(iteration % 3)
            moved = [start[i]+direction[i]*.5 for i in range(2)]
            mouse(0x200, moved, 1)
            mouse(0x202, moved)
            undo()
            assert handles()[1] == view
        # ビュー外の解放は軸の上限で一回確定し、取消し経路は非変更にする。
        start, direction = drag_start(0)
        mouse(0x200, (10000,5000), 1)
        mouse(0x202, (10000,5000))
        assert objects()['player_spawn']['position'] == [8,2,4]
        undo()
        for cancel in ('escape','capture','focus','resize'):
            before = state()
            start, direction = drag_start(2)
            mouse(0x200, [start[i]+direction[i] for i in range(2)], 1)
            if cancel == 'resize':
                user.SetWindowPos(parent,0,0,0,960,800,6)
            else:
                message, wp = {'escape':(0x100,27),'capture':(0x215,0),'focus':(0x8,0)}[cancel]
                user.SendMessageW(view,message,wp,0)
            assert view_state()['transient'] is False
            assert state()['scene'] == before['scene'] and state()['revision']==before['revision']
        start, direction = drag_start(1)
        mouse(0x200, [start[i]+direction[i] for i in range(2)], 1)
        mutate('scene.set_properties',dict(**target(),object_id='player',properties={'position':[1,2,3]}))
        mouse(0x202,start)
        assert objects()['player_spawn']['position']==[1,2,3]
        start, direction = drag_start(0)
        mutate('scene.delete_objects',dict(**target(),object_ids=['player']))
        mouse(0x202,start)
        assert view_state()['selected_id']==''
        undo()
        # 編集用カメラ操作は文書・履歴に入らず、MCPにも同じ値を返す。
        before = state()
        old = view_state()
        mouse(0x204,(200,180),2)
        mouse(0x200,(230,200),2)
        mouse(0x205,(230,200))
        assert abs(view_state()['yaw_degrees']-old['yaw_degrees']+12)<.01
        mouse(0x207,(200,180),16)
        mouse(0x200,(220,190),16)
        mouse(0x208,(220,190))
        assert view_state()['center']!=old['center']
        point = wintypes.POINT(200,180)
        user.ClientToScreen(view,ctypes.byref(point))
        mouse(0x20A,(point.x,point.y),120<<16)
        assert view_state()['height']<old['height']
        assert state()['revision']==before['revision'] and state()['undo_id']==before['undo_id']
        capture('orbited-3d.png')
        assert client.tool('viewport.set_view',dict(**target(),center=[0,0],height=20),uuid.uuid4().hex,True)['error']['code']=='InvalidArgument'
        assert client.tool('viewport.set_view',dict(**target(),center=[0,0,0],height=20,pitch_degrees=90),uuid.uuid4().hex,True)['error']['code']=='InvalidArgument'
        mutate('viewport.reset_view',target())
        click(pixel([12,10,14]))
        assert view_state()['selected_id']=='camera'
        capture('camera-3d.png')
        click(pixel([0,2,-4]))
        user.SendMessageW(view,0x100,46,0)
        assert 'enemy_spawn' not in objects()
        mutate('scene.create_object',dict(**target(),kind='enemy_spawn',properties={'position':[1,2,-3],'fire_interval_seconds':10}))
        mutate('scene.set_settings',dict(**target(),properties={'enemy_hp':2}))
        click(pixel([1,2,3]))
        saved = mutate('document.save',target())
        image = capture('saved-3d.png')
        mutate('document.reload',target())
        assert state()['scene']==saved['scene'] and capture('reloaded-3d.png')==image
        job = mutate('compile.start',dict(project_id=project['project_id'],expected_revision=project['revision'],source='scripts/game.glang'))
        job = client.wait(job['job_id'])
        assert job['state']=='Succeeded'
        old = view_state()
        # 試遊開始は未確定のハンドルを取り消し、保存済み位置で通常の一周を行う。
        drag_start(2)
        play = mutate('play.start',dict(job_id=job['job_id'],preset=0,**target()))
        assert client.tool('viewport.set_view',dict(**target(),center=[0,0,0],height=20),uuid.uuid4().hex,True)['error']['code']=='Busy'
        view = handles()[1]
        user.SendMessageW(view,0x100,13,0)
        user.SendMessageW(view,0x101,13,0)
        user.SendMessageW(view,0x100,32,0)
        deadline = time.monotonic()+10
        while time.monotonic()<deadline:
            playing = client.tool('play.get_state',dict(play_id=play['play_id']))
            if playing['phase']=='Result':
                break
            time.sleep(.03)
        assert playing['phase']=='Result' and playing['outcome']==1, playing
        user.SendMessageW(view,0x101,32,0)
        user.SendMessageW(view,0x100,27,0)
        user.SendMessageW(view,0x101,27,0)
        assert client.tool('play.get_state',dict(play_id=play['play_id']))['phase']=='Start'
        user.SendMessageW(view,0x100,13,0)
        user.SendMessageW(view,0x101,13,0)
        assert client.tool('play.get_state',dict(play_id=play['play_id']))['phase']=='Playing'
        mutate('play.stop',dict(play_id=play['play_id']))
        view = handles()[1]
        current = view_state()
        assert all(current[k]==old[k] for k in ('center','height','yaw_degrees','pitch_degrees'))
        assert state()['scene']==saved['scene']
        capture('final-3d.png')
        (workspace/'accepted-scene.json').write_text(json.dumps(saved['scene'],ensure_ascii=False,indent=2),encoding='utf-8')
        print('3D native XYZ/orbit/pan, GPU capture, cancel/outside release/conflict, save, normal PIE loop passed',flush=True)
        if hold:
            print('Holding Editor for 3D visual acceptance; create finish.txt to close.',flush=True)
            while (workspace/'finish.txt').exists() is False:
                time.sleep(.2)
    finally:
        client.close()


if __name__ == '__main__':
    run(Path(sys.argv[1]).resolve(),Path(sys.argv[2]).resolve(),'--hold' in sys.argv)
