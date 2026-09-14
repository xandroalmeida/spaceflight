class_name Fmt
extends RefCounted
## Números como uma pessoa os lê (regra 74).
##
## O cockpit nunca mostra `384000000.000 m`. Escolher a unidade é o trabalho
## desta classe, e ela é estática porque a escolha não depende de estado nenhum:
## a mesma distância dá a mesma string em qualquer instrumento.
##
## O HUD de debug NÃO passa por aqui de propósito. Lá o que se quer é o número
## cru com todos os dígitos, porque é ele que se confere contra uma tolerância.


## Algarismos significativos, e notação decimal dentro da faixa que se lê sem
## decodificar. Herdado do `_sci` do Milestone 5 e mantido palavra por palavra
## porque a justificativa não mudou: dezesseis dígitos de uma grandeza de ordem
## 1e-13 não são precisão, são ruído.
##
## O `%` do GDScript não tem `%e` -- só `%s %c %d %o %x %X %f %v %%` -- então a
## mantissa é montada à mão.
static func sci(value: float, digits: int = 4) -> String:
	if not is_finite(value) or value == 0.0:
		return "0"
	var exponent := floori(log(absf(value)) / log(10.0))
	if exponent >= -3 and exponent < 6:
		return String.num(value, maxi(digits - 1 - exponent, 0))
	var mantissa := value / pow(10.0, exponent)
	if absf(mantissa) >= 10.0:
		mantissa /= 10.0
		exponent += 1
	return "%se%+d" % [String.num(mantissa, digits - 1), exponent]


const AU_M := 1.495978707e11
const C_MS := 299792458.0


## Distâncias: m -> km -> AU. O degrau para AU é em 0.01 AU (1.5 milhões de km),
## acima do qual "1 496 000 km" já é mais difícil de ler que "0.01 AU".
static func distance(metres: float) -> String:
	if not is_finite(metres):
		return "--"
	var m := absf(metres)
	if m < 1000.0:
		return "%.0f m" % metres
	if m < 0.01 * AU_M:
		return "%s km" % _group(metres / 1000.0, 1 if m < 1.0e7 else 0)
	return "%.4f AU" % (metres / AU_M)


## Velocidades: m/s -> km/s -> fração de c. O degrau para `c` é em 0.001 c
## (300 km/s), que é bem acima de qualquer coisa que uma missão Terra-Lua faz e
## bem abaixo de onde a ótica relativística começa a aparecer.
static func speed(ms: float) -> String:
	if not is_finite(ms):
		return "--"
	var v := absf(ms)
	if v < 1000.0:
		return "%.1f m/s" % ms
	if v < 0.001 * C_MS:
		return "%.3f km/s" % (ms / 1000.0)
	return "%.5f c" % (ms / C_MS)


## Uma duração que vai de segundos a anos sem trocar de instrumento.
static func duration(seconds: float) -> String:
	if not is_finite(seconds) or seconds <= 0.0:
		return "--"
	if seconds < 120.0:
		return "%.1f s" % seconds
	if seconds < 7200.0:
		return "%.1f min" % (seconds / 60.0)
	if seconds < 172800.0:
		return "%.2f h" % (seconds / 3600.0)
	if seconds < 3.15576e7:
		return "%.2f d" % (seconds / 86400.0)
	return "%.3f yr" % (seconds / 3.15576e7)


## Contagem regressiva de missão: `T-01:42:17`. Dias entram só quando existem,
## porque `T-000:01:42:17` esconde o número que interessa atrás de zeros.
static func countdown(seconds: float) -> String:
	if not is_finite(seconds):
		return "T-  --:--:--"
	var sign_text := "T-" if seconds >= 0.0 else "T+"
	var total := int(absf(seconds))
	var days := total / 86400
	var hours := (total % 86400) / 3600
	var minutes := (total % 3600) / 60
	var secs := total % 60
	if days > 0:
		return "%s%dd %02d:%02d:%02d" % [sign_text, days, hours, minutes, secs]
	return "%s%02d:%02d:%02d" % [sign_text, hours, minutes, secs]


static func mass(kg: float) -> String:
	if not is_finite(kg):
		return "--"
	if absf(kg) < 1000.0:
		return "%.1f kg" % kg
	return "%.2f t" % (kg / 1000.0)


static func force(newtons: float) -> String:
	if not is_finite(newtons):
		return "--"
	if absf(newtons) < 1000.0:
		return "%.1f N" % newtons
	if absf(newtons) < 1.0e6:
		return "%.2f kN" % (newtons / 1000.0)
	return "%.2f MN" % (newtons / 1.0e6)


static func percent(fraction: float) -> String:
	return "%.0f %%" % (fraction * 100.0)


static func angle(degrees: float) -> String:
	return "%.2f°" % degrees


## Warp com separador de milhar, porque `100000x` e `1000000x` são
## indistinguíveis de relance e é exatamente aí que a diferença importa.
static func warp(factor: float) -> String:
	return "%sx" % _group(factor, 0)


static func _group(value: float, decimals: int) -> String:
	var text := String.num(value, decimals)
	var negative := text.begins_with("-")
	if negative:
		text = text.substr(1)
	var parts := text.split(".")
	var whole: String = parts[0]
	var grouped := ""
	var count := 0
	for i in range(whole.length() - 1, -1, -1):
		grouped = whole[i] + grouped
		count += 1
		if count % 3 == 0 and i > 0:
			grouped = " " + grouped      # espaço fino: agrupa sem virar vírgula
	if parts.size() > 1:
		grouped += "." + parts[1]
	return ("-" if negative else "") + grouped
