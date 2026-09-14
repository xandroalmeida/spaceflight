extends SceneTree
## Escreve `docs/gameplay/controls.md` a partir do `InputMap` (regra 75).
##
## Gerado e não escrito à mão, pela mesma razão pela qual o painel de ajuda é
## gerado: uma tabela de teclas copiada num documento é uma tabela que envelhece
## em silêncio. Quem quiser mudar uma tecla muda `scripts/input_actions.gd` e
## roda isto.
##
##     godot --headless --path godot/project --script res://scripts/dump_controls.gd

func _initialize() -> void:
	InputActions.install()

	var out := PackedStringArray([
		"# Controles",
		"",
		"> Gerado por `godot/project/scripts/dump_controls.gd` a partir de",
		"> `godot/project/scripts/input_actions.gd`. Não editar à mão: rode",
		"> `scripts/dump_controls.sh` depois de mudar um atalho.",
		"",
		"O mouse:",
		"",
		"| | |",
		"|---|---|",
		"| arrastar com o botão direito | no cockpit, olhar em volta; fora dele, orbitar a nave |",
		"| `Alt` + arrastar | girar a câmera no lugar, deixando o alvo para trás |",
		"| roda | aproximar e afastar (fora do cockpit) |",
		"| clique esquerdo | premir o botão do painel sob o ponteiro |",
		"",
		"As setas movem a CÂMERA; `WASD` move a NAVE. São duas famílias de teclas",
		"porque são duas coisas diferentes: girar a nave queima propelente, girar a",
		"câmera não muda um número do estado.",
		"",
	])

	for group in InputActions.by_group():
		out.append("## %s" % group)
		out.append("")
		out.append("| tecla | ação |")
		out.append("|---|---|")
		for entry in InputActions.by_group()[group]:
			out.append("| `%s` | %s |" % [entry["key"], entry["description"]])
		out.append("")

	out.append("## O que NÃO tem tecla, e porquê")
	out.append("")
	out.append("**Apontar para o alvo** (`T`) está no mapa e responde dizendo que não")
	out.append("está disponível. O controlador de atitude do core aceita LEIS DE")
	out.append("GUIAMENTO -- prógrado, normal, radial -- e \"para onde a Lua está\" não é")
	out.append("uma lei, é uma direção. Dar-lhe uma tecla que falha em voz alta é melhor")
	out.append("do que dar-lhe uma tecla que não existe, e melhor do que inventar um modo")
	out.append("de guiamento no renderizador. Está no backlog.")
	out.append("")
	out.append("**O manche** do console direito não é interativo. Quem pilota é o teclado;")
	out.append("um manche que se mexesse sem comandar nada seria decoração a fingir ser")
	out.append("instrumento.")
	out.append("")

	if not _write("../../docs/gameplay/controls.md", "\n".join(out)):
		quit(1)
		return

	# E a MESMA tabela em JSON, para quem não é um leitor humano.
	#
	# O manual do usuário resolve `{{key:camera_cycle}}` a partir daqui, e falha a
	# compilar se a ação não existir. Sem isso, o manual seria uma terceira cópia
	# das teclas -- e a terceira cópia é sempre a que envelhece, porque ninguém se
	# lembra de que ela existe.
	var machine := {}
	for group in InputActions.by_group():
		for entry in InputActions.by_group()[group]:
			machine[entry["action"]] = {
				"key": entry["key"],
				"group": group,
				"description": entry["description"],
			}
	if not _write("../../docs/gameplay/controls.json", JSON.stringify(machine, "  ", true)):
		quit(1)
		return
	quit(0)


func _write(relative: String, text: String) -> bool:
	var path := ProjectSettings.globalize_path("res://").path_join(relative).simplify_path()
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		push_error("could not write %s" % path)
		return false
	file.store_string(text)
	file.close()
	print("wrote %s" % path)
	return true
