"""サンプルが使う小さな画像・球メッシュ・PCM16 WAVの原本を再生成する。"""
import math
from pathlib import Path
import struct
import wave
import zlib


def generate(root):
    root.mkdir(parents=True, exist_ok=True)
    # RGBAの明るい枠を持つスプライト。色はゲーム側のインスタンス色で指定する。
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

    pixels = bytearray()
    for y in range(16):
        pixels.append(0)
        for x in range(16):
            shade = 255 if min(x, y, 15 - x, 15 - y) < 2 else 224
            pixels.extend((shade, shade, shade, 255))
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 16, 16, 8, 6, 0, 0, 0))
    (root / 'sprite.png').write_bytes(png + chunk(b'IDAT', zlib.compress(pixels)) + chunk(b'IEND', b''))

    # GMESH01: magic、頂点数、index数、position/normal/UVのfloat32、uint32 index。
    vertices = []
    indices = []
    slices = 12
    rings = 8
    for ring in range(rings + 1):
        latitude = math.pi * ring / rings
        for segment in range(slices + 1):
            longitude = 2 * math.pi * segment / slices
            point = (math.sin(latitude) * math.cos(longitude), math.cos(latitude),
                     math.sin(latitude) * math.sin(longitude))
            vertices.append(struct.pack('<8f', *point, *point, segment / slices, ring / rings))
            if ring < rings and segment < slices:
                first = ring * (slices + 1) + segment
                following = first + slices + 1
                indices.extend((first, following, first + 1, first + 1, following, following + 1))
    mesh = b'GMESH01\0' + struct.pack('<II', len(vertices), len(indices)) + b''.join(vertices)
    (root / 'sphere.gmesh').write_bytes(mesh + struct.pack(f'<{len(indices)}I', *indices))

    # 従来の効果音と同じ周波数・長さで、起動時合成を素材読込みへ置き換える。
    for name, frequency in zip(('shot', 'hit', 'clear', 'fail'), (880, 220, 1320, 110)):
        pcm = [int(math.sin(i * frequency * 2 * math.pi / 48000) * 1600 * (1 - i / 2400))
               for i in range(2400)]
        with wave.open(str(root / f'{name}.wav'), 'wb') as output:
            output.setparams((1, 2, 48000, 0, 'NONE', 'not compressed'))
            output.writeframes(struct.pack('<2400h', *pcm))


if __name__ == '__main__':
    generate(Path(__file__).resolve().parent / 'assets')
