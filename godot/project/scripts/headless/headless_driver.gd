class_name HeadlessDriver
extends Node
## A verificação sem tela, preservada do Milestone 5 (regra 62).
##
## Sem display ninguém pode premir uma tecla, então a corrida sem tela voa-se a
## si mesma: aponta, queima, sobe o warp, imprime. É assim que o projeto é
## verificado numa máquina sem ecrã, e é deliberadamente conduzida pela leitura
## COMPLETA, seja qual for o modo do HUD na tela -- a verificação não pode
## depender de para que lado um botão da interface está apontado.
##
## A cadência do print é em QUADROS e não em segundos. A versão do Milestone 2
## imprimia a cada segundo de relógio de parede, o que fazia a verificação
## depender da velocidade da máquina: a 143 fps, `run_godot_headless.sh 200`
## imprimia uma vez -- ou, numa máquina mais rápida, nenhuma, e a saída ficava
## vazia sem nenhum erro.

const PRINT_EVERY_FRAMES := 150

var flight: Node
var mission_mode := false

var _frames := 0
var _slew_commanded := false
var _burn_commanded := false
var _cruise_commanded := false
var _mission_commanded := false
var _mission_armed := false
var _mission_reported := false

## Para onde a corrida sem tela voa. A Lua por omissão, porque é o caso
## qualificado; `SPACEFLIGHT_HEADLESS_DESTINATION=Mars` é como o Milestone 8 se
## verifica de ponta a ponta sem tela.
var _destination := "Moon"


func _init(owner: Node) -> void:
	flight = owner
	mission_mode = OS.get_environment("SPACEFLIGHT_HEADLESS_MISSION") == "1"
	var wanted := OS.get_environment("SPACEFLIGHT_HEADLESS_DESTINATION")
	if not wanted.is_empty():
		_destination = wanted
		mission_mode = true


func drive(text: String) -> void:
	var simulation: SpaceflightSimulation = flight.simulation
	var s := simulation.get_snapshot()
	if s.is_empty():
		return

	if mission_mode:
		_fly_the_mission(s, text)
		return

	# Comanda uma guinada de passagem: o erro de apontamento impresso exercita
	# então a cadeia de atitude inteira -- controlador, RCS, torque, equações de
	# Euler -- de ponta a ponta.
	if not _slew_commanded and s["elapsed_s"] > 2.0:
		_slew_commanded = true
		simulation.set_pointing_mode("prograde")
		print("\n[headless] commanded PROGRADE")
	# Uma vez apontada, queima: o apoapsis tem de subir enquanto o propelente
	# cai. Isso exercita atitude, motor e órbita de uma vez.
	#
	# 3 graus e não 1: um controlador PD assenta no atraso de seguimento
	# 2 zeta n / omega_n = 2,593 graus e nunca chega mais perto
	# (`docs/physics/attitude.md` seção 7.1). Um limiar abaixo disso espera para
	# sempre -- que foi o que a primeira versão fez.
	#
	# Repare na primeira condição: o erro de apontamento é 0 enquanto o modo é
	# HOLD (sem alvo, sem erro), e sem ela a queima dispara imediatamente -- a 90
	# graus do prógrado, que é uma excelente demonstração de um erro de cockpit e
	# uma péssima demonstração de qualquer outra coisa.
	if _slew_commanded and not _burn_commanded \
			and s["pointing_mode"] == "PROGRADE" and s["pointing_error_deg"] < 3.0:
		_burn_commanded = true
		flight.controls.set_throttle(1.0)
		print("\n[headless] throttle 100%% at %.3f deg of pointing error"
			% s["pointing_error_deg"])
	_warp_schedule(s)

	_frames += 1
	if _frames % PRINT_EVERY_FRAMES == 0:
		print("\n" + text)
	# Uma vez, cedo: os números de manchete do milestone, produzidos pelo código
	# que roda e não citados do documento.
	if _frames == PRINT_EVERY_FRAMES:
		for beta in [0.0896, 0.9048, 0.99]:
			print("\n".join(flight.debug_hud.sky_projection_lines(beta)))


func _warp_schedule(snapshot: Dictionary) -> void:
	var elapsed: float = snapshot["elapsed_s"]
	var wanted: int = flight.controls.warp_index
	if not _burn_commanded:
		# A guinada leva um par de minutos de tempo de simulação, o que a warp 1
		# é mais quadros do que qualquer corrida de verificação tem. Warpar
		# através dela não muda nada de físico: o warp decide quanto tempo
		# coordenado um quadro pede, nunca como o propagador lá chega.
		#
		# 10x e não 100x: enquanto o RCS dispara, o controle de erro do próprio
		# propagador mantém os passos curtos, de modo que um quadro a 100x custa
		# dez vezes o trabalho e não compra nada. Medido: 1200 quadros em 8,7 s a
		# 10x contra mais de dois minutos a 100x.
		wanted = 1
	else:
		if elapsed > 2.0e6:
			wanted = FlightControls.WARP_LEVELS.size() - 1      # 1e8
		elif elapsed > 1.0e4:
			wanted = 7                                          # 1e7
		elif elapsed > 6.0e2:
			wanted = 5                                          # 1e5
		else:
			wanted = 2                                          # 100
	if wanted != flight.controls.warp_index:
		flight.controls.warp_index = wanted
		flight.simulation.set_time_warp(flight.controls.warp())

	# CRUISE troca empuxo por velocidade de exaustão: 0,0899 c de orçamento
	# viram 0,9048 c. Trocado assim que a queima de impulso fez a sua parte.
	if not _cruise_commanded and elapsed > 1.0e4:
		_cruise_commanded = true
		flight.simulation.set_engine_mode("CRUISE")
		print("\n[headless] CRUISE -- exhaust 0.5 c, budget 0.9048 c")


func _fly_the_mission(s: Dictionary, text: String) -> void:
	## A missão inteira, sem teclado: escolhe o destino, PROCURA, arma, e deixa o
	## warp levar os dias -- seis e três quartos até à Lua, duzentos e quatro até
	## Marte.
	##
	## `run_mission` parte cada quadro nas épocas de ignição e de corte, de modo
	## que um quadro que abranja uma queima inteira ainda a integra corretamente
	## -- e é por isso que warpar através de uma é seguro aqui e não seria se
	## `advance()` chamasse `propagate()` diretamente. A 1e7x um quadro são
	## 166 667 s e a queima de trim em Marte são 15: sem esse corte ela
	## desapareceria entre dois quadros.
	##
	## A busca agora é assíncrona, então isto é uma máquina de estados e não duas
	## chamadas seguidas. Essa é a diferença que interessa: `plan_mission()`
	## devolve "comecei", não "encontrei".
	if not _mission_commanded and s["elapsed_s"] > 1.0:
		_mission_commanded = true
		flight.set_target(_destination)
		var altitude := 100.0 if _destination == "Moon" else 500.0
		print("\n[headless] searching for a transfer to %s (%.0f km circular)"
			% [_destination, altitude])
		flight.plan_mission(_destination, altitude, altitude)

	# Enquanto a busca corre, o quadro continua a andar -- que é o ponto dela.
	if flight.simulation.is_planning():
		_frames += 1
		if _frames % PRINT_EVERY_FRAMES == 0:
			var progress: Dictionary = flight.simulation.get_planning_progress()
			print("[headless] searching: %s, %d of %d screened, %d flown"
				% [progress.get("stage", "?"), progress.get("candidates_screened", 0),
				   progress.get("candidates_considered", 0),
				   progress.get("candidates_flown", 0)])
		return

	if _mission_commanded and not _mission_armed and flight.simulation.has_planned_transfer():
		_mission_armed = true
		var plan: Dictionary = flight.simulation.get_plan()
		print("\n[headless] plan: %s -> %s, %s, %.0f m/s total"
			% [plan.get("origin", "?"), plan.get("target", "?"),
			   Fmt.duration(plan.get("time_of_flight_days", 0.0) * 86400.0),
			   plan.get("total_delta_v", 0.0)])
		if flight.arm_mission():
			# O warp que faz a viagem caber numa corrida sem tela. Um voo lunar de
			# 6,75 dias a 1e5x são cerca de 1200 quadros; um voo marciano de 204
			# dias precisa de 1e7 para caber no mesmo orçamento.
			flight.controls.warp_index = 5 if _destination == "Moon" else 7
			flight.simulation.set_time_warp(flight.controls.warp())
			print("[headless] armed, warp %s" % Fmt.warp(flight.controls.warp()))

	_frames += 1
	if _frames % PRINT_EVERY_FRAMES == 0:
		print("\n" + text)

	if flight.simulation.has_plan():
		var p: Dictionary = flight.simulation.get_plan()
		if p.get("done", false) and not _mission_reported:
			_mission_reported = true
			print("\n===== ARRIVED =====")
			print("\n".join(flight.debug_hud.hud_lines(false)))
			var outcome: Dictionary = flight.simulation.get_mission_outcome()
			if not outcome.is_empty():
				print("predicted vs actual: %s" % outcome)
			print("===== end =====")
			# E PARA de warpar.
			#
			# Sem isto a corrida continua a 1e7x até esgotar a contagem de quadros,
			# e 15 000 quadros a 1,67e5 s cada são 79 ANOS de tempo simulado: sai da
			# cobertura do de440s, que acaba em 2150, e paga uma propagação de
			# décadas para não mostrar nada. Medido: 20 minutos de relógio de parede
			# depois de a nave já estar em órbita de Marte.
			flight.controls.warp_index = 0
			flight.simulation.set_time_warp(1.0)
