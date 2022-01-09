from PIL import ImageFont
from PIL.ImageDraw import Draw

# The FF SubVario Condensed Medium pixel font.
SUBVARIO_CONDENSED_MEDIUM = {
    'file': 'assets/SubVario-Condensed-Medium.otf',
    'size': 24,
    'height': 20,
    'y_offset': 2,
    'width_overrides': {
        ' ': 5,
        '1': 8
    }
}

# The FF Screenstar Small Regular pixel font.
SCREENSTAR_SMALL_REGULAR = {
    'file': 'assets/Screenstar-Small-Regular.otf',
    'size': 12,
    'height': 10,
    'y_offset': 2,
    'width_overrides': {
        ' ': 3
    }
}

def char_width(draw, font, character, overrides):
    """Returns the width of a given character."""
    # Override the measured width, if specified.
    if character in overrides.keys():
        return overrides[character]
    width, _ = draw.textsize(character, font)
    return width

def draw_text(text, font_spec, text_color, xy=None, anchor=None,
              box_color=None, box_padding=0, border_color=None, border_width=0,
              max_width=None,
              image=None, draw=None):
    """Draws centered text on an image, optionally in a box."""

    if not draw:
        draw = Draw(image)
    text_size = font_spec['size']
    font = ImageFont.truetype(font_spec['file'], size=text_size)

    width_overrides = font_spec['width_overrides']
    textlines = [""]  # text split per line
    character_widths = [[]]  # character widths per line
    for word in text.split():
        # Measure the width of each character.
        widths = [char_width(draw, font, c, width_overrides) for c in word]

        # If appending a word to the given line would exceed max_width,
        # add a new line.
        if max_width and sum(widths) + sum(character_widths[-1]) > max_width:
            character_widths.append([])
            textlines.append("")
        # If current line has some content, add a space before this word.
        elif textlines[-1]:
            character_widths[-1].append(
                char_width(draw, font, ' ', width_overrides))
            textlines[-1] += ' '
        character_widths[-1].extend(widths)
        textlines[-1] += word

    # Width of the longest line is the text width.
    text_width = max([sum(w) for w in character_widths])

    # If any xy is specified, use it.
    text_height = font_spec['height'] * len(textlines)
    if xy:
        x = xy[0] - text_width // 2
        y = xy[1] - text_height // 2

    # If any anchor is specified, adjust the xy.
    if anchor == 'center':
        x = image.width // 2 - text_width // 2
        y = image.height // 2 - text_height // 2
    elif anchor == 'center_x':
        x = image.width // 2 - text_width // 2
    elif anchor == 'center_y':
        y = image.height // 2 - text_height // 2
    elif anchor == 'bottom_right':
        x = image.width - box_padding - border_width - text_width
        y = image.height - box_padding - border_width - text_height

    # Draw the box background and border.
    box_xy = [x - box_padding,
              y - box_padding,
              x + text_width + box_padding,
              y + text_height + box_padding]
    border_xy = [box_xy[0] - border_width,
                 box_xy[1] - border_width,
                 box_xy[2] + border_width,
                 box_xy[3] + border_width]
    if border_color:
        draw.rectangle(border_xy, border_color)
    if box_color:
        draw.rectangle(box_xy, box_color)

    # Draw the text line by line.
    orig_x = x
    for i in range(len(textlines)):
        y -= font_spec['y_offset']
        textline = textlines[i]
        # Draw the line character by character.
        for index in range(len(textline)):
            character = textline[index]
            draw.text((x, y), character, text_color, font)
            x += character_widths[i][index]
        y += font_spec['height']
        x = orig_x

    # Return the bounding box for layout calculations.
    return border_xy
