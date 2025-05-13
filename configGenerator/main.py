import json

default_config = {
    "0xd3": 8,
    "0x111": 0,
    "0x132": 9,
    "xclk": 20,
    "pixformat": 4,
    "framesize": 10,
    "quality": 9,
    "brightness": 0,
    "contrast": 0,
    "saturation": 0,
    "sharpness": 0,
    "special_effect": 0,
    "wb_mode": 0,
    "awb": 1,
    "awb_gain": 1,
    "aec": 1,
    "aec2": 1,
    "ae_level": 0,
    "aec_value": 168,
    "agc": 1,
    "agc_gain": 0,
    "gainceiling": 0,
    "bpc": 1,
    "wpc": 1,
    "raw_gma": 1,
    "lenc": 1,
    "hmirror": 0,
    "dcw": 1,
    "colorbar": 0,
    "led_intensity": 0,
}

code_template = {
    "framesize": "s->set_framesize(s, (framesize_t){val});",
    "quality": "s->set_quality(s, {val});",
    "contrast": "s->set_contrast(s, {val});",
    "brightness": "s->set_brightness(s, {val});",
    "saturation": "s->set_saturation(s, {val});",
    "gainceiling": "s->set_gainceiling(s, (gainceiling_t){val});",
    "colorbar": "s->set_colorbar(s, {val});",
    "awb": "s->set_whitebal(s, {val});",
    "agc": "s->set_gain_ctrl(s, {val});",
    "aec": "s->set_exposure_ctrl(s, {val});",
    "hmirror": "s->set_hmirror(s, {val});",
    "vflip": "s->set_vflip(s, {val});",
    "awb_gain": "s->set_awb_gain(s, {val});",
    "agc_gain": "s->set_agc_gain(s, {val});",
    "aec_value": "s->set_aec_value(s, {val});",
    "aec2": "s->set_aec2(s, {val});",
    "dcw": "s->set_dcw(s, {val});",
    "bpc": "s->set_bpc(s, {val});",
    "wpc": "s->set_wpc(s, {val});",
    "raw_gma": "s->set_raw_gma(s, {val});",
    "lenc": "s->set_lenc(s, {val});",
    "special_effect": "s->set_special_effect(s, {val});",
    "wb_mode": "s->set_wb_mode(s, {val});",
    "ae_level": "s->set_ae_level(s, {val});",
    "led_intensity": "analogWrite(LED_GPIO_NUM, {val});",
}


def generate_code(json_path):
    with open(json_path, "r") as f:
        user_config = json.load(f)

    code_lines = []
    for key, template in code_template.items():
        val = user_config.get(key, default_config.get(key, 0))
        code_lines.append(template.format(val=val))

    return "\n".join(code_lines)


# Example usage
if __name__ == "__main__":
    json_file = "config.json"
    output = generate_code(json_file)
    print(output)
