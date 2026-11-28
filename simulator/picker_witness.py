import hashlib
import json
import struct


def picker_witness(get, post, control, tap):
    """Exercise the shared screen, not the simulator profile shortcut."""
    def require(condition, message):
        if not condition:
            raise RuntimeError('Direct picker: ' + message)

    def pcm(frames):
        block = post('/api/audio', {'frames': frames})
        magic, version, rate, generation, _, count = struct.unpack('<4sIIIII', block[:24])
        require((magic, version, count, len(block)) == (b'CYDA', 1, frames, 24 + 2*frames), 'PCM envelope')
        return rate, generation, sum(v != 0 for v in struct.unpack(f'<{frames}h', block[24:]))

    def choose(slot):
        state = tap(130, 43)
        require(state['page'] == 6, 'menu not opened')
        return tap(30 + 156*(slot % 2), 45 + 38*(slot // 2))

    control('profile', 'grimoire-native')
    control('transport', 'native')
    require(control('connect')['companionReady'], 'initial Grimoire handshake')
    control('page', 'play')
    control('audio-clock', 'on')
    require(json.loads(post('/api/touch', {'phase':'down','x':160,'y':177}))['noteHeld'], 'initial held note')
    before = pcm(4096)
    require(before[0] == 24000 and before[2] > 0, 'initial Grimoire PCM')
    control('page', 'link')
    link_frame = get('/api/frame.png')
    menu = tap(130, 43)
    require(menu['page'] == 6 and not menu['noteHeld'] and not menu['sequence']['running'], 'menu release')
    menu_frame = get('/api/frame.png')
    require(menu_frame != link_frame, 'picker not rendered')
    back = tap(20, 220)
    require(back['page'] == 3 and back['companionReady'] and back['audio']['generation'] == before[1], 'Back retired live link')
    same = choose(6)
    require(same['profile'] == 'grimoire-native' and same['companionReady'] and same['audio']['generation'] == before[1], 'same choice retired live link')
    retired = choose(5)
    require(retired['profile'] == 'xenoresonator-native' and retired['connection'] == 0 and not retired['companionReady'], 'different choice did not retire link')
    offline = pcm(1024)
    require(offline[1] != before[1] and offline[2] == 0, 'retired engine still sounding')
    require(control('connect')['companionReady'], 'explicit Xenoresonator reconnect')
    control('page', 'play')
    require(pcm(1024)[2] == 0, 'old note replayed')
    json.loads(post('/api/touch', {'phase':'up','x':160,'y':177}))
    require(pcm(1024)[2] == 0, 'retired Up replayed')
    require(json.loads(post('/api/touch', {'phase':'down','x':160,'y':177}))['noteHeld'], 'fresh held note')
    after = pcm(4096)
    require(after[0] == 24000 and after[2] > 0, 'fresh Xenoresonator PCM')
    control('panic')
    require(pcm(1024)[2] == 0, 'Panic tail')
    control('page', 'link')
    legacy = []
    for slot, name in enumerate(('melodee', 'sloop', 'skiff', 'x0x')):
        state = choose(slot)
        require(state['profile'] == name and state['connection'] == 0 and not state['companionReady'], 'legacy direct choice ' + name)
        require(pcm(1024)[2] == 0, 'legacy fabricated audio ' + name)
        legacy.append(name)
    state = choose(4)
    require(state['profile'] == 'zaaggenz-native' and state['connection'] == 0, 'ZaaggenZ direct choice')
    control('transport', 'native')
    require(control('connect')['companionReady'], 'ZaaggenZ handshake')
    control('page', 'play')
    json.loads(post('/api/touch', {'phase':'down','x':160,'y':177}))
    zaag = pcm(4096)
    require(zaag[0] == 44100 and zaag[2] > 0, 'ZaaggenZ PCM/rate')
    control('panic')
    require(pcm(1024)[2] == 0, 'ZaaggenZ Panic tail')
    control('audio-clock', 'off')
    return {'result':'PASS', 'legacy_choices':legacy, 'grimoire_nonzero':before[2],
            'xenoresonator_nonzero':after[2], 'zaaggenz_nonzero':zaag[2],
            'retired_nonzero':offline[2], 'back_and_same_keep_generation':True,
            'picker_frame_sha256':hashlib.sha256(menu_frame).hexdigest(),
            'physical_qualification':False}
