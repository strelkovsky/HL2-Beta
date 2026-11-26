"Scheme"
{
    "BaseScheme" "Tracker"

    "Colors"
    {
        "Text.Primary"      "242 239 230"
        "Text.Secondary"    "190 185 173"
        "Accent"            "245 180 91"
        "Accent.Dim"        "191 144 84"
        "Surface.Panel"     "101 97 91 152"
        "Surface.Raised"    "81 78 71 230"
        "Surface.Inset"     "26 24 21 214"
        "Selection.Focused" "103 76 43 218"
        "Selection.Unfocused" "72 55 38 235"
        "Border.Light"      "143 137 126 232"
        "Border.Shadow"     "30 27 23 245"

        "ControlBG"        "Surface.Panel"
        "ControlDarkBG"    "Surface.Raised"
        "ControlText"      "Text.Primary"
        "BaseText"         "Text.Primary"
        "DimBaseText"      "Text.Secondary"
        "BrightBaseText"   "Text.Primary"
        "BrightControlText" "Text.Primary"
        "BorderBright"     "Border.Light"
        "BorderDark"       "Border.Shadow"
        "BorderSelection"  "Accent.Dim"
        "SelectionBG"      "Selection.Focused"
        "SelectionBG2"     "Selection.Unfocused"
    }

    "BaseSettings"
    {
        "FgColor"                    "Text.Primary"
        "BgColor"                    "Surface.Panel"
        "FrameCornerRadius"          "8"
        "TitleBarFgColor"            "Text.Primary"
        "TitleBarBgColor"            "0 0 0 0"
        "TitleBarDisabledFgColor"    "Text.Secondary"
        "TitleBarDisabledBgColor"    "0 0 0 0"
        "TitleButtonFgColor"         "Text.Primary"
        "TitleButtonBgColor"         "0 0 0 0"
        "TitleButtonDisabledFgColor" "Text.Secondary"
        "TitleButtonDisabledBgColor" "0 0 0 0"
        "FgColorDim"                 "Text.Secondary"
        "CheckBgColor"               "Surface.Inset"
        "CheckButtonBorder1"         "Border.Shadow"
        "CheckButtonBorder2"         "Border.Light"
        "CheckButtonCheck"           "Accent"
        "ButtonArmedFgColor"         "Text.Primary"
        "ButtonArmedBgColor"         "ControlBG"
        "ButtonDepressedFgColor"     "Text.Primary"
        "ButtonDepressedBgColor"     "Surface.Raised"
        "WindowFgColor"              "Text.Primary"
        "WindowBgColor"              "Surface.Inset"
        "WindowDisabledFgColor"      "Text.Secondary"
        "WindowDisabledBgColor"      "Surface.Panel"
        "ListBgColor"                "Surface.Inset"
        "BuddyListBgColor"           "Surface.Inset"
        "ListSelectionFgColor"       "Text.Primary"
        "SectionTextColor"           "Text.Secondary"
        "SectionDividerColor"        "Border.Light"

        "InGameDesktop"
        {
            "MenuColor"              "255 255 255 255"
            "ArmedMenuColor"         "200 200 200 255"
            "DepressedMenuColor"     "192 186 80 255"
            "GameMenuInset"          "36"
            "MenuItemHeight"         "30"
            "MenuMinimumWidth"       "196"
            "MenuTextInset"          "0"
            "MenuItemVisibilityRate" "0.035"
        }
    }

    "Fonts"
    {
        "Default"
        {
            "1"
            {
                "name" "Tahoma"
                "tall" "14"
                "weight" "400"
                "antialias" "1"
            }
        }

        "WindowTitle"
        {
            "1"
            {
                "name" "Tahoma"
                "tall" "14"
                "weight" "700"
                "antialias" "1"
            }
        }

        "ReleaseTitle"
        {
            "1"
            {
                "name" "HalfLife2"
                "tall" "46"
                "weight" "0"
                "antialias" "1"
            }
        }

        "MenuLarge"
        {
            "1"
            {
                "name" "Verdana"
                "tall" "16"
                "weight" "600"
                "antialias" "1"
            }
        }
    }

    "CustomFontFiles"
    {
        "1" "resource/HALFLIFE2.ttf"
    }

    "Borders"
    {
        "BaseBorder"
        {
            "inset" "1 1 1 1"
            "Left"   { "1" { "color" "Border.Light"  "offset" "0 0" } }
            "Top"    { "1" { "color" "Border.Light"  "offset" "0 0" } }
            "Right"  { "1" { "color" "Border.Shadow" "offset" "0 0" } }
            "Bottom" { "1" { "color" "Border.Shadow" "offset" "0 0" } }
        }
        "ButtonBorder" "BaseBorder"
        "FrameBorder" {}
        "TitleButtonBorder" "FrameBorder"
        "TitleButtonDepressedBorder" "FrameBorder"
        "TitleButtonDisabledBorder" "FrameBorder"
    }
}
