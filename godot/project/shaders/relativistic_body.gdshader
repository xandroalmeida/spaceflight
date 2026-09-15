shader_type spatial;
// world_vertex_coords puts VERTEX in SCENE units on the way in and on the way
// out.  That is not a convenience: the light-cone equation compares a length
// against c * dtau, so the vertex has to be in the same units as the c that is
// passed in, and model space is scaled by the body's radius.
render_mode world_vertex_coords;

// A body as it is SEEN: per-vertex light time, and colour shifted by the Doppler
// factor of its centre.
//
// Two things happen here, and only one of them is physics this shader owns.
//
//   colour   D arrives as a uniform from core/relativity/optics.hpp, and the
//            shader applies what D means -- exactly as star_field.gdshader does.
//
//   shape    the per-vertex retarded time, which is per-vertex BY DEFINITION and
//            therefore cannot live anywhere else.  This is the one formula in
//            the project that exists twice; it mirrors
//            sf::render::retarded_light_time() and
//            sf::render::lorentz_contract() in core/render/terrell.hpp, and what
//            is done about the duplication is
//            docs/architecture/relativistic-shaders.md section 5: the test pins
//            the RESULT (arcsin beta to 1e-5 deg, a round silhouette to 3e-4)
//            rather than the source text.
//
// Nobody rotates anything.  Terrell rotation is what comes out.

uniform sampler2D planck_table : filter_linear, repeat_disable;
uniform float table_reference_temperature = 6000.0;

// Doppler factor of the body centre, from optics.hpp.
uniform float doppler = 1.0;
uniform float beaming_doppler = 1.0;

// The body's velocity relative to the observer, and the speed of light, BOTH in
// scene units per second.  The scale cancels out of the retarded time, which is
// why they must share it: a c in metres per second against a position in scene
// units would put the effect seven orders of magnitude out and it would simply
// vanish (docs/architecture/relativistic-shaders.md section 4).
uniform vec3 relative_velocity_scene = vec3(0.0);
uniform float light_speed_scene = 299.792458;

// The observer, in scene coordinates.  The body centre comes from MODEL_MATRIX,
// and it was already placed by core/relativity/light_time.hpp against the real
// ephemeris -- minutes of light time along a curved path -- so this shader
// computes only the DIFFERENTIAL within the body and must not retard the centre
// a second time.
uniform vec3 observer_position_scene = vec3(0.0);

uniform vec3 reflectance : source_color = vec3(0.5);

// --- surface, added in Milestone 7 ------------------------------------------
//
// Everything below is APPEARANCE. None of it reaches the vertex stage, none of
// it changes a Doppler factor, and turning it all off gives back exactly the
// Milestone 5 image: `reflectance` alone, multiplied by the same ratio.
//
// What changed for it: the illuminant ratio used to be multiplied into the
// colour in vertex() and is now carried to fragment() as `colour_ratio` and
// multiplied there. Same product, later -- but per-pixel, which is what a
// texture needs. At D = 1 the ratio is exactly vec3(1) and the surface is the
// texel, which is the invariant worth keeping (rule 47: a generated image is
// never physics; it says what the body LOOKS like and nothing else).
uniform sampler2D albedo_map : source_color, filter_linear_mipmap, repeat_enable;
uniform bool use_albedo_map = false;
uniform sampler2D night_map : source_color, filter_linear_mipmap, repeat_enable;
uniform bool use_night_map = false;
// A máscara de nuvens NÃO leva `source_color`, e isso não é esquecimento: no
// Godot 4 é o hint do uniforme que decide se a amostra passa por sRGB, não o
// importador. Uma cobertura é uma fração, não uma cor, e passá-la pela curva
// sRGB distorceria a coisa que ela mede.
uniform sampler2D cloud_map : filter_linear_mipmap, repeat_enable;
uniform bool use_cloud_map = false;
uniform float cloud_offset = 0.0;

// A curva da máscara, medida e não escolhida.
//
// ⚠️ Ela é medida CONTRA UM MAPA. Trocar o mapa sem refazer a medida põe a
// cobertura onde calhar, e foi o que aconteceu quando o mapa gerado deu lugar ao
// composto MODIS: com a curva antiga (82..204) o mapa novo dava 58 % de céu
// limpo e 4 % de nuvem cheia, uma Terra quase sem nuvens.
//
// Histograma do composto MODIS da NASA, em bytes: p10 2, p25 14, p50 60, p75
// 128, p90 178. Remapeado para 41..153, sobram 42 % de cobertura e 42 % de céu
// genuinamente limpo -- menos que os 67 % reais da Terra, e é isso que se quer:
// as nuvens têm de deixar ver os continentes que estão por baixo. São os mesmos
// dois números que o mapa anterior produzia, sobre um mapa com outro histograma.
//
// (O mapa gerado, para comparação: p05 21, p25 38, p50 115, p75 191, p95 230 --
// muito mais claro, porque a cobertura dele era quase total e sem as regiões
// limpas que a circulação atmosférica produz.)
uniform float cloud_low = 0.16;
uniform float cloud_high = 0.60;
uniform float cloud_opacity = 0.88;

// The Sun, as a direction in scene coordinates. Used for the night side and for
// the limb -- both of which are questions about where the terminator is, and the
// terminator is not something a renderer should guess.
uniform vec3 sun_direction_scene = vec3(1.0, 0.0, 0.0);
// O relevo. `hint_normal` e NÃO `source_color`: uma normal é dado, não cor, e
// passá-la pela curva sRGB torceria os vetores.
//
// A tangente vem da malha, calculada sobre os vértices EM REPOUSO, e a etapa de
// vértice acima desloca-os pelo tempo retardado. A diferença é de segunda ordem
// em β e some a velocidades orbitais; a β relativístico o relevo de um planeta
// fica com a orientação da tangente de repouso, o que está registrado como
// VISUAL_DEBT em vez de corrigido -- corrigi-lo pede a derivada do deslocamento,
// que é uma conta por vértice a mais para um efeito que só existe onde o
// planeta já está deformado por Terrell.
uniform sampler2D normal_map : hint_normal, filter_linear_mipmap, repeat_enable;
uniform bool use_normal_map = false;
uniform float normal_strength = 1.0;

// Quanto as luzes de cidade emitem. 1,4 era o valor com a textura procedural,
// cujas luzes eram pontos fracos; o mapa entregue tem aglomerados inteiros
// saturados e a 1,4 o lado noturno ficava âmbar de costa a costa.
uniform float night_energy = 0.75;
uniform float atmosphere_strength = 0.0;
// A dispersão sobre o DISCO, não só no limbo.
//
// `atmosphere_strength` acende a borda, onde a linha de vista atravessa mais ar.
// Mas há ar sobre o disco inteiro, e é ele que faz um oceano -- cujo albedo real
// é 0,06, quase preto -- ler azul-marinho numa fotografia de órbita em vez de
// ler betume. Sem este termo a Terra fica correta no albedo e escura demais na
// imagem, que foi o que apareceu quando as texturas geradas deram lugar às
// medidas: a cobertura de nuvens caiu de quase total para os 42 % reais, e o que
// ficou à vista foi oceano cru.
//
// ⚠️ DESENHO, e não física, exatamente como o termo do limbo ao lado: não há
// modelo atmosférico neste projeto e a regra 31 diz que não deve haver. É um
// número escolhido para que a imagem se pareça com uma fotografia, e está
// separado do outro para que se possa mexer num sem mexer no outro.
uniform float atmosphere_haze = 0.0;
uniform vec3 atmosphere_colour : source_color = vec3(0.35, 0.55, 0.95);

// Planets reflect; they are not black bodies.  The illuminant is, so the shift
// acts on sunlight and the albedo multiplies it
// (docs/physics/relativistic-rendering.md section 9.4).
uniform float illuminant_temperature = 5772.0;
// A star (the Sun) emits its own black body instead, at its own temperature.
uniform bool is_self_luminous = false;
uniform float half_saturation = 0.1584893;

// Off, the body is drawn where it geometrically is.  Provided so the difference
// can be seen rather than argued about; the STATE is identical either way.
uniform bool apply_light_time = true;

varying vec3 shifted_colour;
varying vec3 colour_ratio;
varying vec3 world_normal;
varying vec3 world_position;

float table_index(float temperature) {
    return temperature / (temperature + table_reference_temperature);
}

// |p - v dtau| = c dtau, solved.  Mirrors sf::render::retarded_light_time().
//
// No clamp and no branch: the discriminant is structurally positive because
// c^2 - v^2 > 0 is guaranteed by the state (rule 13) and survives the scaling
// above exactly.
float retarded_light_time(vec3 offset, vec3 velocity, float c) {
    float pv = dot(offset, velocity);
    float c2_minus_v2 = c * c - dot(velocity, velocity);
    float discriminant = pv * pv + c2_minus_v2 * dot(offset, offset);
    return (sqrt(discriminant) - pv) / c2_minus_v2;
}

// Mirrors sf::render::lorentz_contract().  A mesh is the body's shape in ITS OWN
// frame; this is what makes it a set of positions in the observer's frame at one
// instant, which is the question the light cone can then be asked about.
//
// This is NOT what rule 38 forbids.  Forbidden is contraction as the ANSWER --
// drawing the squashed mesh.  Omitting it does not give "no contraction": it
// gives a silhouette 160% out of round, against 9e-5 with it
// (docs/physics/relativistic-rendering.md section 11.5).
vec3 lorentz_contract(vec3 rest_offset, vec3 velocity, float c) {
    float speed = length(velocity);
    if (speed <= 0.0) {
        return rest_offset;
    }
    vec3 direction = velocity / speed;
    float beta = speed / c;
    float inverse_gamma = sqrt(1.0 - beta * beta);
    float along = dot(rest_offset, direction);
    return rest_offset - direction * along + direction * (along * inverse_gamma);
}

void vertex() {
    vec3 centre = MODEL_MATRIX[3].xyz;
    vec3 centre_from_observer = centre - observer_position_scene;

    if (apply_light_time) {
        // The mesh, in world orientation, is the body's shape in its own rest
        // frame.  Two steps, in this order (section 11.5).
        vec3 rest_offset = VERTEX - centre;
        vec3 contracted = lorentz_contract(rest_offset, relative_velocity_scene, light_speed_scene);

        vec3 p = centre_from_observer + contracted;
        float dtau_vertex = retarded_light_time(p, relative_velocity_scene, light_speed_scene);
        float dtau_centre =
            retarded_light_time(centre_from_observer, relative_velocity_scene, light_speed_scene);

        // The differential, added back to the centre the CPU already placed.
        vec3 apparent_offset = (p - relative_velocity_scene * dtau_vertex) -
                               (centre_from_observer - relative_velocity_scene * dtau_centre);
        VERTEX = centre + apparent_offset;
    }

    vec4 rest = textureLod(planck_table, vec2(table_index(illuminant_temperature), 0.5), 0.0);
    vec4 shifted =
        textureLod(planck_table, vec2(table_index(illuminant_temperature * doppler), 0.5), 0.0);

    float band = exp(shifted.a - rest.a);
    float luminance = pow(beaming_doppler, 4.0) * band;
    float response = luminance / (luminance + half_saturation);

    world_normal = normalize(MODEL_NORMAL_MATRIX * NORMAL);
    world_position = VERTEX;

    if (is_self_luminous) {
        // A star: it emits its own black body, and the beaming acts on it.
        shifted_colour = shifted.rgb * response;
        colour_ratio = vec3(1.0);
    } else {
        // A planet: reflectance times a shifted illuminant.  At D = 1 the ratio
        // is exactly 1 and the colour is the tabulated albedo -- anything else
        // would mean the scene lies while standing still.  The division is safe
        // for any illuminant above ~1500 K, where every tabulated channel is
        // strictly positive; the epsilon is float hygiene, not a limit.
        colour_ratio = shifted.rgb / max(rest.rgb, vec3(1e-6));
        shifted_colour = reflectance * colour_ratio;
    }
}

// Amostragem equirretangular sem a linha na costura.
//
// Em `u = 0 ≡ 1` a coordenada salta de 1 para 0 entre dois pixels vizinhos. A
// GPU escolhe o nível de mipmap a partir da DERIVADA de `u`, e nessa coluna a
// derivada é ~1 em vez de ~1e-3: ela escolhe o mip mais grosseiro e desenha uma
// linha visível de um polo ao outro. Na captura do planeta inteiro ela cortava o
// Pacífico exatamente na longitude ±180, que é onde a costura está.
//
// A correção é dizer à GPU a derivada verdadeira: se o salto é maior que meia
// textura, ele é o embrulho e não um gradiente, e subtrai-se uma volta.
vec4 sample_wrapped(sampler2D map, vec2 uv) {
    vec2 dx = dFdx(uv);
    vec2 dy = dFdy(uv);
    if (abs(dx.x) > 0.5) { dx.x -= sign(dx.x); }
    if (abs(dy.x) > 0.5) { dy.x -= sign(dy.x); }
    return textureGrad(map, uv, dx, dy);
}

void fragment() {
    // A star emits its own black body; a planet reflects the Sun's. One branch,
    // no early return -- GLSL's fragment stage in Godot has an implicit epilogue
    // and `return` skips it.
    if (is_self_luminous) {
        ALBEDO = vec3(0.0);
        EMISSION = shifted_colour;
    } else {
        vec3 surface = reflectance;
        if (use_albedo_map) {
            surface = sample_wrapped(albedo_map, UV).rgb;
        }

        // Clouds: a second layer with its own longitude offset, over the surface
        // and under everything else. The offset is a DRAWING parameter -- there
        // is no atmospheric circulation in this project and pretending otherwise
        // with a number that drifts would be inventing weather.
        if (use_cloud_map) {
            float raw = sample_wrapped(cloud_map, vec2(fract(UV.x + cloud_offset), UV.y)).r;
            float cloud = smoothstep(cloud_low, cloud_high, raw);
            surface = mix(surface, vec3(0.92, 0.94, 0.96), cloud * cloud_opacity);
        }

        ALBEDO = surface * colour_ratio;

        if (use_normal_map) {
            NORMAL_MAP = sample_wrapped(normal_map, UV).rgb;
            NORMAL_MAP_DEPTH = normal_strength;
        }

        // How lit this pixel is, from the direction of the Sun. Godot's own
        // lighting does the shading; this only decides which side is night.
        vec3 normal = normalize(world_normal);
        float sun = dot(normal, normalize(sun_direction_scene));
        vec3 emitted = vec3(0.0);

        if (use_night_map) {
            // City lights, fading in across the terminator rather than switching
            // at it: a hard line at sun = 0 reads as a rendering artefact, which
            // is what it would be.
            float night = smoothstep(0.08, -0.16, sun);
            emitted += sample_wrapped(night_map, UV).rgb * night * night_energy;
        }

        if (atmosphere_strength > 0.0) {
            // The limb: air is thickest where the line of sight grazes the
            // surface, which is where the normal is perpendicular to the view.
            // Brightened only on the lit side, because an unlit limb does not
            // glow.
            vec3 to_view = normalize(world_position - CAMERA_POSITION_WORLD);
            float grazing = 1.0 - abs(dot(normal, to_view));
            float limb = pow(clamp(grazing, 0.0, 1.0), 3.0);
            float lit = clamp(sun * 1.4 + 0.25, 0.0, 1.0);
            emitted += atmosphere_colour * limb * lit * atmosphere_strength;
            // Sem o `limb`: o ar do disco inteiro, apagando-se no terminador
            // pelo mesmo `lit`, porque um céu no lado escuro não dispersa nada.
            emitted += atmosphere_colour * lit * atmosphere_haze;
        }

        EMISSION = emitted;
    }
}
