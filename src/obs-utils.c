#include "obs-utils.h"

gs_texrender_t *create_or_reset_texrender(gs_texrender_t *render)
{
	if (!render) {
		render = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	} else {
		gs_texrender_reset(render);
	}
	return render;
}

void set_blending_parameters()
{
	gs_blend_state_push();
	gs_reset_blend_state();
	gs_enable_blending(false);
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
}

void set_render_parameters()
{
	// Culling
	gs_set_cull_mode(GS_NEITHER);

	// Enable all colors.
	gs_enable_color(true, true, true, true);

	// Depth- No depth test.
	gs_enable_depth_test(false);
	gs_depth_function(GS_ALWAYS);

	// Setup Stencil- No test, no write.
	gs_enable_stencil_test(false);
	gs_enable_stencil_write(false);
	gs_stencil_function(GS_STENCIL_BOTH, GS_ALWAYS);
	gs_stencil_op(GS_STENCIL_BOTH, GS_ZERO, GS_ZERO, GS_ZERO);
}

bool add_source_to_list(void *data, obs_source_t *source)
{
	obs_property_t *p = data;
	const char *name = obs_source_get_name(source);
	size_t count = obs_property_list_item_count(p);
	size_t idx = 0;
	while (idx < count &&
	       strcmp(name, obs_property_list_item_string(p, idx)) > 0)
		idx++;
	obs_property_list_insert_string(p, idx, name, name);
	return true;
}

void texrender_set_texture(gs_texture_t *source, gs_texrender_t *dest)
{
	gs_effect_t *pass_through = obs_get_base_effect(OBS_EFFECT_DEFAULT);

	uint32_t w = gs_texture_get_width(source);
	uint32_t h = gs_texture_get_height(source);

	gs_eparam_t *image = gs_effect_get_param_by_name(pass_through, "image");
	gs_effect_set_texture(image, source);
	set_blending_parameters();
	if (gs_texrender_begin(dest, w, h)) {
		gs_ortho(0.0f, (float)w, 0.0f, (float)h, -100.0f, 100.0f);
		while (gs_effect_loop(pass_through, "Draw"))
			gs_draw_sprite(source, 0, w, h);
		gs_texrender_end(dest);
	}
	gs_blend_state_pop();
}

// Loads the shader file at `effect_file_path` into *effect
gs_effect_t *load_shader_effect(gs_effect_t *effect,
				const char *effect_file_path)
{
	if (effect != NULL) {
		obs_enter_graphics();
		gs_effect_destroy(effect);
		effect = NULL;
		obs_leave_graphics();
	}
	char *shader_text = NULL;
	struct dstr filename = {0};
	dstr_cat(&filename, obs_get_module_data_path(obs_current_module()));
	dstr_cat(&filename, effect_file_path);
	shader_text = load_shader_from_file(filename.array);
	char *errors = NULL;

	obs_enter_graphics();
	effect = shader_text ? gs_effect_create(shader_text, NULL, &errors) : NULL;
	obs_leave_graphics();

	bfree(shader_text);

	if (effect == NULL) {
		blog(LOG_WARNING,
		     "[obs-composite-blur] Unable to load .effect file.  Errors:\n%s",
		     (errors == NULL || strlen(errors) == 0 ? "(None)"
							    : errors));
		bfree(errors);
	}

	dstr_free(&filename);

	return effect;
}

/* Limit both recursion and total input: corrupt includes must fail cleanly. */
static char *load_shader_recursive(const char *file_name, unsigned depth, size_t *budget)
{
	const size_t file_limit = 1024 * 1024;
	if (!file_name || depth >= 16 || !*budget) return NULL;
	FILE *stream = os_fopen(file_name, "rb");
	if (!stream) return NULL;
	size_t limit = *budget < file_limit ? *budget : file_limit;
	char *file = bmalloc(limit + 1);
	size_t count = fread(file, 1, limit, stream);
	bool failed = ferror(stream) || fgetc(stream) != EOF;
	fclose(stream);
	if (failed) { bfree(file); return NULL; }
	file[count] = 0;
	*budget -= count;
	char **lines = strlist_split(file, '\n', true);
	struct dstr shader_file = {0};
	for (size_t i = 0; lines[i]; ++i) {
		const char *line = lines[i];
		if (i == 0 && strncmp(line, "\xEF\xBB\xBF", 3) == 0) line += 3;
		while (*line == ' ' || *line == '\t') ++line;
		if (strncmp(line, "#include", 8) == 0) {
			const char *start = strchr(line, '"');
			const char *end = start ? strchr(start + 1, '"') : NULL;
			if (!start || !end || end == start + 1) { failed = true; break; }
			const char *slash = strrchr(file_name, '/');
			const char *backslash = strrchr(file_name, '\\');
			if (!slash || (backslash && backslash > slash)) slash = backslash;
			struct dstr path = {0};
			if (slash) dstr_ncopy(&path, file_name, slash - file_name + 1);
			dstr_ncat(&path, start + 1, end - start - 1);
			char *included = load_shader_recursive(path.array, depth + 1, budget);
			dstr_free(&path);
			if (!included) { failed = true; break; }
			dstr_cat(&shader_file, included);
			bfree(included);
		} else {
			dstr_cat(&shader_file, line);
		}
		dstr_cat(&shader_file, "\n");
		if (shader_file.len > 8 * 1024 * 1024) { failed = true; break; }
	}
	bfree(file);
	strlist_free(lines);
	if (failed) { dstr_free(&shader_file); return NULL; }
	return shader_file.array ? shader_file.array : bstrdup("");
}

char *load_shader_from_file(const char *file_name)
{
	size_t budget = 8 * 1024 * 1024;
	char *shader = load_shader_recursive(file_name, 0, &budget);
	if (!shader) blog(LOG_WARNING, "[obs-composite-blur] Shader missing, malformed, or include limit exceeded.");
	return shader;
}