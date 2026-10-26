
from PIL import Image
import sys

def png_to_bin(input_file, output_file, threshold=128):
    # Load PNG and place transparency over white
    img = Image.open(input_file).convert("RGBA")
    background = Image.new("RGBA", img.size, "white")
    img = Image.alpha_composite(background, img).convert("L")

    # Convert to 1-bit: black = 1, white = 0
    pixels = [1 if p < threshold else 0 for p in img.getdata()]

    # Pack pixels MSB-first, with no row padding
    data = bytearray()
    for i in range(0, len(pixels), 8):
        byte = 0
        for bit in range(8):
            if i + bit < len(pixels):
                byte |= pixels[i + bit] << (7 - bit)
        data.append(byte)

    with open(output_file, "wb") as f:
        f.write(data)

    print(f"Converted {input_file} ({img.width}x{img.height})")
    print(f"Output: {output_file} ({len(data)} bytes)")
    print(f"Use dimensions: {img.width}, {img.height}")

if __name__ == "__main__":
    png_to_bin(sys.argv[1], sys.argv[2])
