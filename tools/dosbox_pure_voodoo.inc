// SPDX-License-Identifier: GPL-2.0-or-later
// NEO98: the 3dfx Voodoo drawn by the host. tools/build_dosbox_pure.py copies
// this file into the patched clone of the core and includes it into
// src/hardware/voodoo.cpp behind voodoo_ogl_mainthread.
// Upstream draws the card either in software or with OpenGL. Its OpenGL path
// has two halves: the DOS thread records what the card is asked to draw
// (voodoo_ogl_draw_triangle and its neighbours fill a command buffer and
// decode textures), and the thread of retro_run plays the record with OpenGL
// calls (voodoo_ogl_mainthread). NEO98 keeps the first half and replaces the
// second: the record goes to the host as a DosVoodooFrame
// (src/dos_voodoo_frame.h), and the host draws it through Gpu::Backend. The
// patches in voodoo.cpp add to the record what the host's pixel pipeline
// wants and OpenGL's did not: the registers unmasked, 1/w and Z per vertex
// instead of a depth computed per vertex, and the mip levels of a texture.
//

// Whether the host draws the card.
static INLINE bool neo98_voodoo_sink() { return DBP_Neo98Active() && DBP_Neo98State()->voodoo_frame; }

// One mip level of a TMU's texture as 0xAARRGGBB, the way upstream decodes
// the largest level in voodoo_ogl_draw_triangle.
static void neo98_voodoo_decode_level(const tmu_state& tmu, UINT32 lod, UINT8 tformat, rgb_t* out)
{
	const UINT32 smax = (tmu.wmask >> lod) + 1, tmax = (tmu.hmask >> lod) + 1, tmask = tmu.mask;
	const rgb_t* tlookup = tmu.lookup;
	const UINT8* tmuram = tmu.ram;
	UINT32 at = tmu.lodoffset[lod];
	rgb_t* end = out + smax * tmax;
	if (tformat < 8)
		for (; out != end;) *(out++) = tlookup[tmuram[at++ & tmask]];
	else if (tformat >= 10 && tformat <= 12)
		for (; out != end; at += 2) *(out++) = tlookup[*(const UINT16*)&tmuram[at & tmask]];
	else
		for (; out != end; at += 2)
		{
			const UINT16 texel = *(const UINT16*)&tmuram[at & tmask];
			*(out++) = (tlookup[texel & 0xFF] & 0xFFFFFF) | ((texel & 0xff00) << 16);
		}
}

// The levels below the largest one, behind it in the buffer of the upload:
// every level tLOD allows, as long as each is half the one before. A texture
// split over both TMUs by odd and even levels keeps its largest level alone.
static void neo98_voodoo_levels(ogl_texupload& tu, const tmu_state& tmu, UINT32 ilod, UINT8 tformat)
{
	tu.neo98_levels = 1;
	rgb_t* out = tu.buf + tu.smax * tu.tmax;
	for (UINT32 lod = ilod + 1, last = (UINT32)(tmu.lodmax >> 8); lod <= last && lod <= 8; lod++)
	{
		if (!((tmu.lodmask >> lod) & 1)) break;
		const UINT32 smax = (tmu.wmask >> lod) + 1, tmax = (tmu.hmask >> lod) + 1;
		neo98_voodoo_decode_level(tmu, lod, tformat, out);
		out += smax * tmax;
		tu.neo98_levels++;
	}
}

// The buffers of DosVoodooReadback a command draws into: its colour buffer,
// and the depth when the auxiliary buffer is written.
static INLINE unsigned neo98_voodoo_touches(const ogl_command& cmd)
{
	if (cmd.base.type == ogl_cmdbase::CLIPPING) return 0;
	const UINT32 fbz = (cmd.base.type == ogl_cmdbase::FASTFILL ? cmd.fastfill.fbz_mode :
		cmd.base.type == ogl_cmdbase::TRIANGLE ? cmd.geometry.neo98_fbz_mode : cmd.geometry.eff.fbz_mode);
	return (DosVoodooReadback::Color << (cmd.base.drawbuffer % 3)) | (FBZMODE_AUX_BUFFER_MASK(fbz) ? (unsigned)DosVoodooReadback::Depth : 0u);
}

// What the program may read through the linear frame buffer, in bits of
// DosVoodooReadback.
static struct
{
	unsigned valid;     // these buffers of the readback hold what the host has drawn
	unsigned pending;   // the commands of the record before `scanned` draw into these
	UINT32 scanned;
	unsigned wanted;    // read since a read last cut the record
} neo98_lfb;

// A saved state was put in: the buffers of the card in its memory are those
// of the state, and the host's are not yet (voodoo_neo98_frame hands them over).
static bool neo98_restore;

static GrowArray<DosVoodooCommand> neo98_commands;
static GrowArray<DosVoodooVertex> neo98_vertices;
static GrowArray<DosVoodooTexture> neo98_textures;
static float neo98_gamma[6] = { 1, 1, 1, 1, 1, 1 };

// The first commands of the record with their vertices, and every texture
// decoded since the last frame, as a frame for the host. Answers the buffers
// the commands draw into.
static unsigned neo98_voodoo_build(DosVoodooFrame& frame, const UINT32 flush_commands, const UINT32 flush_vertices)
{
	GrowArray<DosVoodooCommand>& commands = neo98_commands;
	GrowArray<DosVoodooVertex>& vertices = neo98_vertices;
	GrowArray<DosVoodooTexture>& textures = neo98_textures;
	unsigned touched = 0;
	commands.Reset();
	vertices.Reset();
	textures.Reset();

	for (ogl_texupload& tu : vogl->texuploads)
	{
		DosVoodooTexture& t = textures.AddOne();
		t = DosVoodooTexture();
		t.index = tu.textureidx;
		t.width = (unsigned)tu.smax;
		t.height = (unsigned)tu.tmax;
		t.levels = tu.neo98_levels;
		t.texels = tu.buf;
	}

	for (UINT32 c = 0; c != flush_commands; c++)
	{
		const ogl_command& cmd = vogl->cmdbuf.commands.data[c];
		touched |= neo98_voodoo_touches(cmd);
		DosVoodooCommand& out = commands.AddOne();
		out = DosVoodooCommand();
		out.buffer = cmd.base.drawbuffer;
		if (cmd.base.type == ogl_cmdbase::FASTFILL || cmd.base.type == ogl_cmdbase::CLIPPING)
		{
			const bool fill = (cmd.base.type == ogl_cmdbase::FASTFILL);
			const ogl_clipping& clip = (fill ? cmd.fastfill.clip : cmd.clipping.clip);
			out.type = (fill ? DosVoodooCommand::Fill : DosVoodooCommand::Clip);
			out.clip_active = (clip.active != 0);
			out.clip[0] = clip.sx; out.clip[1] = clip.sy; out.clip[2] = clip.ex; out.clip[3] = clip.ey;
			if (fill)
			{
				out.fbz_mode = cmd.fastfill.fbz_mode;
				out.constant1 = ((UINT32)cmd.fastfill.col1.a << 24) | ((UINT32)cmd.fastfill.col1.r << 16) | ((UINT32)cmd.fastfill.col1.g << 8) | cmd.fastfill.col1.b;
				out.za_color = cmd.fastfill.zacolor;
				out.auxiliary = (cmd.fastfill.auxoffs != (UINT32)(~0));
			}
			continue;
		}

		const UINT32 first = cmd.vertex_index, next = (c + 1 != flush_commands ? vogl->cmdbuf.commands.data[c + 1].vertex_index : flush_vertices);
		const ogl_geometrycmd& g = cmd.geometry;
		const bool triangles = (cmd.base.type == ogl_cmdbase::TRIANGLE);
		out.type = (triangles ? DosVoodooCommand::Triangles : cmd.base.type == ogl_cmdbase::PIXEL_RAW ? DosVoodooCommand::RawPixels : DosVoodooCommand::BlendedPixels);
		out.first_vertex = vertices.num;
		out.vertex_count = next - first;
		out.fbz_mode = (triangles ? g.neo98_fbz_mode : g.eff.fbz_mode);
		out.color_path = g.eff.color_path;
		out.alpha_mode = g.eff.alpha_mode;
		out.fog_mode = g.eff.fog_mode;
		out.constant0 = g.uni.col0.u;
		out.constant1 = g.uni.col1.u;
		out.chroma_key = g.uni.chromakey.u;
		out.fog_color = g.uni.fogcolor.u;
		out.za_color = (triangles ? g.neo98_zacolor : 0);
		for (int t = 0; t != 2; t++)
		{
			if (g.eff.tex_mode[t] == VOODOO_OGL_TEXMODE_DISABLED) continue;
			out.textured[t] = true;
			out.texture_mode[t] = g.eff.tex_mode[t];
			out.lod[t] = g.neo98_lod[t];
			out.detail[t] = g.neo98_detail[t];
			out.texture[t] = g.textureidx[t];
			// What voodoo_ogl_mainthread notes when it binds a texture: the
			// frame that last drew with it.
			vogl->textures.data[g.textureidx[t]].lastframe = vogl->renderframe;
		}

		const ogl_vertex* in = vogl->cmdbuf.vertices.data + first;
		DosVoodooVertex* vo = vertices.Add(next - first);
		if (triangles)
			for (UINT32 i = 0; i != next - first; i++, in++, vo++)
			{
				vo->x = in->neo98_x; vo->y = in->neo98_y; vo->z = in->neo98_z; vo->oow = in->neo98_oow;
				vo->r = in->r * 255.0f; vo->g = in->g * 255.0f; vo->b = in->b * 255.0f; vo->a = in->neo98_a * 255.0f;
				vo->s0 = in->neo98_m[0].s; vo->t0 = in->neo98_m[0].t; vo->q0 = in->neo98_m[0].q;
				vo->s1 = in->neo98_m[1].s; vo->t1 = in->neo98_m[1].t; vo->q1 = in->neo98_m[1].q;
			}
		else
			for (UINT32 i = 0; i != next - first; i++, in++, vo++)
			{
				// A pixel of the linear frame buffer: its middle, from the top.
				vo->x = in->x; vo->y = in->y; vo->z = in->d * 65535.0f; vo->oow = 1.0f;
				vo->r = in->r * 255.0f; vo->g = in->g * 255.0f; vo->b = in->b * 255.0f; vo->a = in->a * 255.0f;
				vo->s0 = vo->t0 = vo->q0 = vo->s1 = vo->t1 = vo->q1 = 0.0f;
			}
	}

	if (v->ogl_clutDirty)
	{
		FitGammaCurves(v, neo98_gamma[0], neo98_gamma[1], neo98_gamma[2], neo98_gamma[3], neo98_gamma[4], neo98_gamma[5]);
		v->ogl_clutDirty = false;
	}
	frame.active = true;
	frame.width = v->fbi.width;
	frame.height = v->fbi.height;
	frame.y_origin = v->fbi.yorigin;
	frame.shown = vogl->flushed_buffer;
	frame.commands = commands.data;
	frame.command_count = commands.num;
	frame.vertices = vertices.data;
	frame.vertex_count = vertices.num;
	frame.textures = textures.data;
	frame.texture_count = textures.num;
	memcpy(frame.fog_table, v->fbi.fogblend, sizeof(frame.fog_table));
	for (int i = 0; i != 3; i++) { frame.gamma_exponent[i] = neo98_gamma[i]; frame.gamma_factor[i] = neo98_gamma[3 + i]; }
	return touched;
}

// The commands, vertices and uploads handed to the host leave the record, as
// at the end of voodoo_ogl_mainthread.
static void neo98_voodoo_cut(const UINT32 flush_commands, const UINT32 flush_vertices)
{
	vogl->texuploads.Reset();
	if (UINT32 late_commands = (vogl->cmdbuf.commands.num - flush_commands))
		memmove(vogl->cmdbuf.commands.data, vogl->cmdbuf.commands.data + flush_commands, late_commands * sizeof(*vogl->cmdbuf.commands.data));
	vogl->cmdbuf.commands.num -= flush_commands;
	vogl->cmdbuf.flushed_commands -= (flush_commands < vogl->cmdbuf.flushed_commands ? flush_commands : vogl->cmdbuf.flushed_commands);
	for (ogl_command& c : vogl->cmdbuf.commands) c.vertex_index -= flush_vertices;
	if (UINT32 late_vertices = (vogl->cmdbuf.vertices.num - flush_vertices))
		memmove(vogl->cmdbuf.vertices.data, vogl->cmdbuf.vertices.data + flush_vertices, late_vertices * sizeof(*vogl->cmdbuf.vertices.data));
	vogl->cmdbuf.vertices.num -= flush_vertices;
	vogl->cmdbuf.flushed_vertices -= (flush_vertices < vogl->cmdbuf.flushed_vertices ? flush_vertices : vogl->cmdbuf.flushed_vertices);
	// What is left is looked through again by the next read.
	neo98_lfb.scanned = 0;
	neo98_lfb.pending = 0;
}

// A read through the linear frame buffer: two pixels of the buffer lfbMode
// selects. A card empties its FIFO before such a read, so the program sees
// all it drew before. The record is the FIFO here: when it holds a command
// that draws into the buffer read, or the host's copy of that buffer is older
// than what the host drew, everything recorded goes to the host now, on the
// DOS thread, with the buffers the host is to bring back; the sink returns
// when it has drawn them and filled the readback. Reads that follow without
// a draw between take the same copy. A program that reads several buffers
// after each draw gets them in one turn from the second time on.
static UINT32 neo98_voodoo_read_pixel(int x, int y)
{
	Neo98DbpState& state = *DBP_Neo98State();
	DosVoodooReadback& readback = state.voodoo_readback;
	const unsigned short* buffer;
	unsigned bit;
	switch (LFBMODE_READ_BUFFER_SELECT(v->reg[lfbMode].u))
	{
		case 0: buffer = readback.color[v->fbi.frontbuf % 3]; bit = DosVoodooReadback::Color << (v->fbi.frontbuf % 3); break;
		case 1: buffer = readback.color[v->fbi.backbuf % 3]; bit = DosVoodooReadback::Color << (v->fbi.backbuf % 3); break;
		case 2: buffer = readback.depth; bit = DosVoodooReadback::Depth; break;
		default: return 0xFFFFFFFF;
	}
	if (!buffer || x < 0 || y < 0 || (unsigned)x + 1 >= DosVoodooReadback::Pitch || (unsigned)y >= DosVoodooReadback::Rows) return 0xFFFFFFFF;

	ogl_cmdbuffer& cmdbuf = vogl->cmdbuf;
	for (; neo98_lfb.scanned < cmdbuf.commands.num; neo98_lfb.scanned++)
		neo98_lfb.pending |= neo98_voodoo_touches(cmdbuf.commands.data[neo98_lfb.scanned]);
	neo98_lfb.wanted |= bit;
	if ((neo98_lfb.pending | ~neo98_lfb.valid) & bit)
	{
		DosVoodooFrame frame;
		const UINT32 all_commands = cmdbuf.commands.num, all_vertices = cmdbuf.vertices.num;
		const unsigned touched = neo98_voodoo_build(frame, all_commands, all_vertices);
		frame.read_mask = neo98_lfb.wanted;
		state.voodoo_frame(state.voodoo_owner, frame);
		neo98_voodoo_cut(all_commands, all_vertices);
		// The next triangle or pixel opens a command of its own.
		cmdbuf.last_geometry.drawbuffer = 255;
		neo98_lfb.valid = (neo98_lfb.valid & ~touched) | frame.read_mask;
		neo98_lfb.wanted = bit;
	}
	const unsigned short* p = buffer + (size_t)y * DosVoodooReadback::Pitch + x;
	return (UINT32)p[0] | ((UINT32)p[1] << 16);
}

// Saved states. The pixels of the card are the host's;
// the memory of the card, which a state holds, has only what the program
// wrote there itself. Before a state is taken everything recorded goes to
// the host, as for a read through the linear frame buffer, and the three
// colour buffers and the depth come back into the memory of the card. Called
// between two frames on the thread of retro_run; the sink returns when the
// host has drawn and filled the readback.
static void neo98_voodoo_write_back()
{
	if (!neo98_voodoo_sink() || !vogl || !vogl_active || !v || !v->active) return;
	// A state was put in and no frame ran since: the memory of the card
	// holds the buffers, the host does not have them yet.
	if (neo98_restore) return;
	Neo98DbpState& state = *DBP_Neo98State();
	DosVoodooReadback& readback = state.voodoo_readback;
	ogl_cmdbuffer& cmdbuf = vogl->cmdbuf;
	const unsigned all = (DosVoodooReadback::Color * 7u) | DosVoodooReadback::Depth;
	for (; neo98_lfb.scanned < cmdbuf.commands.num; neo98_lfb.scanned++)
		neo98_lfb.pending |= neo98_voodoo_touches(cmdbuf.commands.data[neo98_lfb.scanned]);
	if ((neo98_lfb.pending | ~neo98_lfb.valid) & all)
	{
		DosVoodooFrame frame;
		const UINT32 all_commands = cmdbuf.commands.num, all_vertices = cmdbuf.vertices.num;
		neo98_voodoo_build(frame, all_commands, all_vertices);
		frame.read_mask = all;
		state.voodoo_frame(state.voodoo_owner, frame);
		neo98_voodoo_cut(all_commands, all_vertices);
		cmdbuf.last_geometry.drawbuffer = 255;
		neo98_lfb.valid = all;
		neo98_lfb.wanted = 0;
	}
	const UINT32 width = (v->fbi.width < DosVoodooReadback::Pitch ? v->fbi.width : DosVoodooReadback::Pitch);
	const UINT32 height = (v->fbi.height < DosVoodooReadback::Rows ? v->fbi.height : DosVoodooReadback::Rows);
	const UINT32 pitch = v->fbi.rowpixels, size = v->fbi.mask + 1;
	for (int i = 0; i != 4; i++)
	{
		const UINT32 offs = (i < 3 ? v->fbi.rgboffs[i] : v->fbi.auxoffs);
		const unsigned short* from = (i < 3 ? readback.color[i] : readback.depth);
		if (!from || offs == (UINT32)(~0) || width > pitch || (UINT64)offs + (UINT64)height * pitch * 2 > size) continue;
		for (UINT32 y = 0; y != height; y++)
			memcpy(v->fbi.ram + offs + (size_t)y * pitch * 2, from + (size_t)y * DosVoodooReadback::Pitch, (size_t)width * 2);
	}
}

// The buffers of the card as its memory holds them after a state was put
// in, for the host to take over with the next frame.
static void neo98_voodoo_restore(DosVoodooFrame& frame)
{
	const UINT32 pitch = v->fbi.rowpixels, size = v->fbi.mask + 1;
	if (!pitch || v->fbi.width > pitch) return;
	for (int i = 0; i != 4; i++)
	{
		const UINT32 offs = (i < 3 ? v->fbi.rgboffs[i] : v->fbi.auxoffs);
		if (offs == (UINT32)(~0) || (UINT64)offs + (UINT64)v->fbi.height * pitch * 2 > size) continue;
		(i < 3 ? frame.restore_color[i] : frame.restore_depth) = (const unsigned short*)(v->fbi.ram + offs);
	}
	frame.restore_pitch = pitch;
}

// Called by retro_run in place of voodoo_ogl_mainthread, while the DOS thread
// stands still: what was recorded up to the last vertical blank goes to the
// host, and the record is cut the way voodoo_ogl_mainthread cuts it.
void voodoo_neo98_frame()
{
	Neo98DbpState& state = *DBP_Neo98State();
	DosVoodooFrame frame;
	if (!vogl_active || !v || !v->active)
	{
		neo98_lfb.valid = neo98_lfb.pending = neo98_lfb.scanned = neo98_lfb.wanted = 0;
		state.voodoo_frame(state.voodoo_owner, frame);
		return;
	}
	const UINT32 flush_commands = vogl->cmdbuf.flushed_commands, flush_vertices = vogl->cmdbuf.flushed_vertices;
	neo98_lfb.valid &= ~neo98_voodoo_build(frame, flush_commands, flush_vertices);
	if (neo98_restore) { neo98_voodoo_restore(frame); neo98_restore = false; neo98_lfb.valid = 0; }
	state.voodoo_frame(state.voodoo_owner, frame);
	neo98_voodoo_cut(flush_commands, flush_vertices);

	// From here on as the end of voodoo_ogl_mainthread: textures nothing drew
	// with for a while become free for other texels.
	if (vogl->textures.num >= 32)
	{
		bool marked = false;
		for (UINT32 i = vogl->renderframe * 32, j = i + 32; i != j; i++)
		{
			const UINT32 idx = (i % vogl->textures.num);
			ogl_texture& tex = vogl->textures.data[idx];
			if ((vogl->renderframe - tex.lastframe) < 600 || tex.lastframe == ogl_texture::FREED_LASTFRAME) continue;
			if (!marked)
			{
				// Commands recorded but not yet played still draw with their textures.
				for (const ogl_command& cmd : vogl->cmdbuf.commands)
					if (cmd.base.type == ogl_cmdbase::TRIANGLE)
						for (int t = 0; t != 2; t++)
							if (cmd.geometry.eff.tex_mode[t] != VOODOO_OGL_TEXMODE_DISABLED)
								vogl->textures.data[cmd.geometry.textureidx[t]].lastframe = vogl->renderframe;
				marked = true;
				if (tex.lastframe == vogl->renderframe) continue;
			}
			tex.lastframe = ogl_texture::FREED_LASTFRAME;
			vogl->free_textures.AddOne() = idx;
			vogl->texture_hashes.Remove(tex.key);
			for (ogl_texbase& tb : vogl->texbases)
				if (tb.textureidx == idx) tb.textureidx = (UINT32)-1;
		}
	}
	vogl->renderframe++;
	vogl_showing = true;
	vogl->display_buffer = vogl->flushed_buffer;
}
