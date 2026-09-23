#include "app/presentation/controls_doc.hpp"

#include "app/presentation/input_actions.hpp"

#include <map>

namespace sf::app::controls_doc {

std::string markdown() {
    std::string out =
        "# Controles\n"
        "\n"
        "> Gerado por `spaceflight --dump-controls` a partir de\n"
        "> `app/presentation/input_actions.cpp`. Não editar à mão: rode\n"
        "> `scripts/dump_controls.sh` depois de mudar um atalho.\n"
        "\n"
        "O mouse:\n"
        "\n"
        "| | |\n"
        "|---|---|\n"
        "| arrastar com o botão direito | no cockpit, olhar em volta; fora dele, orbitar a nave |\n"
        "| `Alt` + arrastar | girar a câmera no lugar, deixando o alvo para trás |\n"
        "| roda | aproximar e afastar (fora do cockpit) |\n"
        "| clique esquerdo | premir o botão do painel sob o ponteiro |\n"
        "\n"
        "As setas movem a CÂMERA; `WASD` move a NAVE. São duas famílias de teclas\n"
        "porque são duas coisas diferentes: girar a nave queima propelente, girar a\n"
        "câmera não muda um número do estado.\n"
        "\n";
    for (const auto& group : input::by_group()) {
        out += "## " + group.name + "\n\n| tecla | ação |\n|---|---|\n";
        for (const auto* binding : group.entries) {
            out += "| `" + input::label(binding->action) + "` | " + binding->description + " |\n";
        }
        out += "\n";
    }
    out +=
        "## O que NÃO tem tecla, e porquê\n"
        "\n"
        "**Apontar para o alvo** (`T`) está no mapa e responde dizendo que não\n"
        "está disponível. O controlador de atitude do core aceita LEIS DE\n"
        "GUIAMENTO -- prógrado, normal, radial -- e \"para onde a Lua está\" não é\n"
        "uma lei, é uma direção. Dar-lhe uma tecla que falha em voz alta é melhor\n"
        "do que dar-lhe uma tecla que não existe, e melhor do que inventar um modo\n"
        "de guiamento no renderizador. Está no backlog.\n"
        "\n"
        "**O manche** do console direito não é interativo. Quem pilota é o teclado;\n"
        "um manche que se mexesse sem comandar nada seria decoração a fingir ser\n"
        "instrumento.\n";
    return out;
}

std::string json() {
    // Sorted by action, each entry's keys sorted too: the file is diffed, and a
    // stable order is what makes a diff mean something.
    std::map<std::string, const Binding*> sorted;
    for (const auto& binding : input::bindings()) {
        sorted.emplace(binding.action, &binding);
    }
    std::string out = "{\n";
    bool first = true;
    for (const auto& [action, binding] : sorted) {
        out += first ? "" : ",\n";
        first = false;
        out += "  \"" + action + "\": {\n";
        out += "    \"description\": \"" + std::string{binding->description} + "\",\n";
        out += "    \"group\": \"" + std::string{binding->group} + "\",\n";
        out += "    \"key\": \"" + input::label(action) + "\"\n";
        out += "  }";
    }
    out += "\n}";
    return out;
}

}  // namespace sf::app::controls_doc
