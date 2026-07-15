extends RefCounted


func run(token_result: Dictionary, authenticated_start: Callable) -> Dictionary:
	if not token_result.get("ok", false):
		return token_result
	return authenticated_start.call(str(token_result.get("result", "")))
