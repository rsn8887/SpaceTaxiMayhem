-- Lays out the Space Taxi Mayhem .dmg window: background art, big icons, no toolbar or
-- status bar, window exactly the size of the background (640 x 420 pt).
-- Run by CPack (CPACK_DMG_DS_STORE_SETUP_SCRIPT) with the volume name as argument.
-- Icon positions are icon centres and match the arrow in macos/dmg-background.png.
-- Finder only writes the layout (.DS_Store) lazily, so the window is closed, reopened and
-- nudged to force a save, and the script waits until the .DS_Store file exists.
on run argv
	set volumeName to item 1 of argv
	set winX to 200
	set winY to 120
	set winW to 640
	set winH to 420 + 28 -- content height + title bar
	tell application "Finder"
		tell disk volumeName
			open
			tell container window
				set current view to icon view
				set toolbar visible to false
				set statusbar visible to false
				set the bounds to {winX, winY, winX + winW, winY + winH}
				set statusbar visible to false
			end tell
			set viewOptions to the icon view options of container window
			tell viewOptions
				set arrangement to not arranged
				set icon size to 128
				set text size to 13
			end tell
			set background picture of viewOptions to file ".background:background.png"
			set position of item "Space Taxi Mayhem.app" of container window to {160, 236}
			set position of item "Applications" of container window to {480, 236}
			update without registering applications
			close
			open
			-- nudge the size so Finder registers a change and saves it
			delay 1
			tell container window
				set statusbar visible to false
				set the bounds to {winX, winY, winX + winW - 10, winY + winH - 10}
			end tell
		end tell
		delay 1
		tell disk volumeName
			tell container window
				set statusbar visible to false
				set the bounds to {winX, winY, winX + winW, winY + winH}
			end tell
		end tell
		-- give Finder time to write .DS_Store (at most ~15 s)
		set dsStore to (POSIX path of (disk volumeName as alias)) & ".DS_Store"
		delay 2
		repeat 15 times
			if (do shell script "[ -f " & quoted form of dsStore & " ]; echo $?") = "0" then exit repeat
			delay 1
		end repeat
		tell disk volumeName to close
	end tell
end run
