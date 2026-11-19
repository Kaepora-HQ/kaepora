# FAQ

## The game runs slowly or stutters. What should I change?

Kaepora plays in Lichii, so how smoothly the game runs depends on Lichii's settings and your computer. Try these in order, and check the game after each one.

1. **Use Lichii's recommended settings.** Choose **Help → Apply Recommended Settings…**. A new install of Lichii already starts with them, so this matters if you changed settings or have used Lichii for a while. It sets **GPU Mode** to Fast, video decoding to the graphics card and asynchronous presentation on, and puts the other performance settings on its list back to their defaults. **Help → Restore My Previous Settings** undoes it.
2. **Build shaders before the game starts.** Open **Emulation → Configure…**, choose **Graphics**, then the **Advanced** tab. Set **Precache shaders before game start** to **Always**. The first launch spends a few minutes on the loading screen. Later launches only build what is new. Do this after step 1, because step 1 turns it off.
3. **If the picture flickers,** set **GPU Mode** to **Accurate** on the same Advanced tab.
4. **Check the resolution.** Lichii starts at **1×**. If you raised **Resolution** on the **Graphics** tab, set it back to 1×. On a slow computer, try **0.75×**.
5. **Check the console mode.** Lichii starts in **Handheld** mode, which is lighter on your computer than Docked mode. If you switched to Docked, try Handheld again with the **Docked / Handheld** button in the bar at the bottom of Lichii's window.
6. **Let the game lower its own resolution.** Right-click the game in Lichii's list and choose **Configure Game**. On the **Add-Ons** tab, turn off **Max Resolution (built-in)**. The game then draws a smaller picture whenever your graphics card is busy, as it does on a Switch 2.

Lichii needs at least 16 GB of RAM for Switch 2 games, and 32 GB is better.

## Why does the game stutter the first time I enter a place?

The first time the game draws something new, Lichii has to build a shader for it, and that takes a moment. Lichii keeps every shader it builds, so your next visit is smooth. Step 2 above builds many of them before you start playing.

If places you visited long ago still stutter, also turn on **Build all cached shaders before game start** on the same Advanced tab. Loading then takes longer and uses more memory.

## Which resolution should I choose?

Lichii can play the game like a Switch 2 in your hands or in its dock. It starts in **Handheld** mode. Switch with the **Docked / Handheld** button in the bar at the bottom of Lichii's window. Lichii's **Resolution** setting multiplies the size the game draws at:

| Resolution | Handheld | Docked |
| --- | --- | --- |
| 0.75× | 1440×810 | 1920×1080 |
| 1× (Lichii's default) | 1920×1080 | 2560×1440 |
| 1.5× | 2880×1620 | 3840×2160 (4K) |
| 2× | 3840×2160 (4K) | 5120×2880 |

Choose the row whose size matches your screen. A higher setting looks sharper and needs a faster graphics card.

Lichii's menu lists the original Switch's sizes, such as "1X (720p/1080p)", and marks some choices as experimental. Go by this chart instead.
