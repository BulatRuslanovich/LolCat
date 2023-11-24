using System;
using System.Collections.Generic;

class Program {
    static void Main() {
		List<Color> xtermColors = new List<Color>();
		List<int> valueRange = new List<int> {0x00, 0x5f, 0x87, 0xaf, 0xd7, 0xff};

		for (int i = 0; i < 217; i++)
		{
			int r = valueRange[(i / 36) % 6];
			int g = valueRange[(i / 6) % 6];
			int b = valueRange[i % 6];

			//System.Console.WriteLine($"{r} {g} {b}");
			xtermColors.Add(new Color(r, g, b));
		} 


		// оттенки серого
		for (int i = 1; i < 24; i++) {
			int v = 8 + i * 10;
			xtermColors.Add(new Color(v, v, v));
		}

		Console.WriteLine("/* GENERATED HEADER FILE */");

		Console.WriteLine("union rgb_c xterm256Palette[0xff - 0x10 + 0x01] = {");
		foreach (var color in xtermColors) {
			Console.Write("\t{{");
			Console.Write("0x{0:X2}, 0x{1:X2}, 0x{2:X2}", color.B, color.G, color.R);
			Console.Write("}},\n");
		}

		Console.WriteLine("};");
	}

	private struct Color {
		public int R;
		public int G;
		public int B;

		public Color(int r, int g, int b) {
			R = r;
			G = g;
			B = b;
		}
	}
}
