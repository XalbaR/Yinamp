# GitHub repository presentation

## Repository description

Native Windows media player with a Winamp Classic-inspired UI, local playback and native YouTube streaming/downloads without FFmpeg or yt-dlp.

## Recommended topics

`cpp`, `cpp23`, `windows`, `win32`, `media-foundation`, `direct2d`, `media-player`, `youtube`, `youtube-player`, `audio-player`, `video-player`, `equalizer`, `fft`, `retro`, `winamp`

These 15 topics describe the implemented platform, media pipeline, interface and audio processing. GitHub allows up to 20 topics, each at most 50 characters, using lowercase letters, numbers and hyphens. See [GitHub topic rules](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/classifying-your-repository-with-topics).

## Apply metadata to the fresh public repository

On 2026-10-11, origin resolved to `XalbaR/Yinamp`, but the authenticated GitHub CLI reported `isPrivate: true`. No GitHub metadata was changed.

Only run the edit commands after confirming that `XalbaR/Yinamp` is the intended fresh public repository, rather than a private archive. Confirm the Git remote separately; a public visibility value alone does not establish that it is the intended repository.

```powershell
git remote -v
gh repo view XalbaR/Yinamp --json nameWithOwner,isPrivate,url
```

Once that identity and public status are confirmed:

```powershell
gh repo edit XalbaR/Yinamp --description "Native Windows media player with a Winamp Classic-inspired UI, local playback and native YouTube streaming/downloads without FFmpeg or yt-dlp."
gh repo edit XalbaR/Yinamp --add-topic cpp,cpp23,windows,win32,media-foundation,direct2d,media-player,youtube,youtube-player,audio-player,video-player,equalizer,fft,retro,winamp
```

Verify the saved values:

```powershell
gh repo view XalbaR/Yinamp --json description,repositoryTopics
```

## Application screenshot

**Clean application screenshot still required.** No real application screenshot was found in the repository. The social preview is a branded card, not an application screenshot.

Capture an actual clean running application with no personal playlist, file paths, account/history information, signed URLs or debug/test windows. Check that the UI is in a stable state. Save the reviewed capture as `assets/github/yinamp-screenshot.png`, then place this image immediately after the opening product description in the root README:

```markdown
![Yinamp on Windows](assets/github/yinamp-screenshot.png)
```

Do not add the image reference until that real file exists and has been reviewed.

## Social preview

[social-preview.png](social-preview.png) is a 1280 x 640 opaque PNG below 1 MB. It uses geometry from Yinamp's original [application icon](../icon/Yinamp.svg), Segoe UI typography and a slate/gold layout. It contains no application screenshot, third-party brand logos or personal data.

To regenerate with Pillow installed and Windows Segoe UI fonts available:

```powershell
python assets/github/generate_social_preview.py
```

Upload manually on the intended public repository:

GitHub repository → Settings → Social preview → Edit → Upload image → `assets/github/social-preview.png`

This file has not been uploaded automatically. Its dimensions and file size follow [GitHub's social preview guidance](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/customizing-your-repositorys-social-media-preview).
