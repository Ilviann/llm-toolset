extends RefCounted

const ErrorEnvelope := preload("error_envelope.gd")
const Limits := preload("command_limits.gd")

const CAPTURE_FOLDER := "res://.godot/godot_mcp/captures"
const CAPTURE_TTL_SECONDS := 120

var _context


func _init(context) -> void:
	_context = context


func capture_game_view(request_id: String, arguments: Dictionary) -> Dictionary:
	var run_check: Dictionary = _context.requested_run(arguments)
	if not run_check.ok:
		return run_check
	var width_result: Dictionary = _context.bounded_integer(
		arguments.get("max_width", 1280), "Maximum capture width", 1,
		Limits.MAX_CAPTURE_OUTPUT_WIDTH,
	)
	if not width_result.ok:
		return width_result
	var height_result: Dictionary = _context.bounded_integer(
		arguments.get("max_height", 720), "Maximum capture height", 1,
		Limits.MAX_CAPTURE_OUTPUT_HEIGHT,
	)
	if not height_result.ok:
		return height_result
	var started := int(Time.get_ticks_msec())
	if DisplayServer.get_name() == "headless":
		return ErrorEnvelope.failure(
			"Game-view capture is unavailable with the headless display server",
			ErrorEnvelope.UNSUPPORTED_CAPABILITY, {}, false,
		)
	var viewport: Window = _context.ignored_node.get_tree().root
	if viewport == null:
		return ErrorEnvelope.failure(
			"The game viewport is unavailable", ErrorEnvelope.UNSUPPORTED_CAPABILITY,
		)
	var image: Image = viewport.get_texture().get_image()
	if image == null or image.is_empty():
		return ErrorEnvelope.failure(
			"The renderer did not provide a game viewport image",
			ErrorEnvelope.UNSUPPORTED_CAPABILITY, {}, true,
		)
	var source_width := image.get_width()
	var source_height := image.get_height()
	if (
		source_width < 1
		or source_height < 1
		or source_width > Limits.MAX_CAPTURE_SOURCE_WIDTH
		or source_height > Limits.MAX_CAPTURE_SOURCE_HEIGHT
		or source_width * source_height > Limits.MAX_CAPTURE_SOURCE_PIXELS
	):
		return ErrorEnvelope.failure(
			"The source viewport exceeds capture limits", ErrorEnvelope.INVALID_ARGUMENT,
			{"width": source_width, "height": source_height}, false,
		)
	var scale := minf(
		1.0,
		minf(
			float(width_result.result) / float(source_width),
			float(height_result.result) / float(source_height),
		),
	)
	var output_width := maxi(1, int(floorf(float(source_width) * scale)))
	var output_height := maxi(1, int(floorf(float(source_height) * scale)))
	if output_width * output_height > Limits.MAX_CAPTURE_OUTPUT_PIXELS:
		return _context.failure("Capture output pixel limit was exceeded")
	if output_width != source_width or output_height != source_height:
		image.resize(output_width, output_height, Image.INTERPOLATE_LANCZOS)
	var folder := ProjectSettings.globalize_path(CAPTURE_FOLDER)
	var directory_error := DirAccess.make_dir_recursive_absolute(folder)
	if directory_error != OK and directory_error != ERR_ALREADY_EXISTS:
		return ErrorEnvelope.failure(
			"Could not create the capture staging folder", ErrorEnvelope.SAVE_FAILED,
		)
	var capture_path := folder.path_join(request_id + ".png")
	if FileAccess.file_exists(capture_path):
		return ErrorEnvelope.failure(
			"Capture staging identity already exists", ErrorEnvelope.STALE_OPERATION,
		)
	var save_error := image.save_png(capture_path)
	if save_error != OK:
		return ErrorEnvelope.failure(
			"Could not stage the game viewport capture", ErrorEnvelope.SAVE_FAILED,
			{"error": save_error}, false,
		)
	var encoded_bytes := FileAccess.get_file_as_bytes(capture_path).size()
	var elapsed := int(Time.get_ticks_msec()) - started
	if (
		encoded_bytes < 8
		or encoded_bytes > Limits.MAX_CAPTURE_BYTES
		or elapsed > Limits.CAPTURE_TIMEOUT_MSEC
	):
		DirAccess.remove_absolute(capture_path)
		return ErrorEnvelope.failure(
			"Game viewport capture exceeded its bounds",
			ErrorEnvelope.TIMEOUT if elapsed > Limits.CAPTURE_TIMEOUT_MSEC else ErrorEnvelope.INVALID_ARGUMENT,
			{"bytes": encoded_bytes, "elapsed_ms": elapsed}, false,
		)
	return _context.success({
		"capture_id": request_id,
		"run_id": _context.run_id,
		"source_width": source_width,
		"source_height": source_height,
		"width": output_width,
		"height": output_height,
		"bytes": encoded_bytes,
		"format": "png",
	})


func cleanup_stale_captures() -> void:
	var folder := ProjectSettings.globalize_path(CAPTURE_FOLDER)
	var directory := DirAccess.open(folder)
	if directory == null:
		return
	var cutoff := int(Time.get_unix_time_from_system()) - CAPTURE_TTL_SECONDS
	for file_name in directory.get_files():
		if file_name.ends_with(".png"):
			var path := folder.path_join(file_name)
			if int(FileAccess.get_modified_time(path)) < cutoff:
				DirAccess.remove_absolute(path)
