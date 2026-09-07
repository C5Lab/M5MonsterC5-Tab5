"""Build a Discord payload locally; this module never sends notifications."""

import json
import os
import re
import sys


def clean_description(body, release_url, flasher_url):
    repo_url = release_url.split('/releases/')[0]

    def redundant(url):
        url = url.rstrip('/').lower()
        return (
            url == flasher_url.rstrip('/').lower()
            or url == release_url.rstrip('/').lower()
            or url.startswith(repo_url.lower() + '/releases/download/')
            or url.startswith(repo_url.lower() + '/archive/')
        )

    lines = []
    for line in body.replace('\r\n', '\n').replace('\r', '\n').split('\n'):
        if re.fullmatch(r'\s*Release \S+ published\.\s*', line):
            continue
        # Remove entire Markdown links, then autolinks and bare URLs. Keep
        # surrounding prose and unrelated links (PRs, documentation, etc.).
        line = re.sub(
            r'\[[^\]]*\]\((https?://[^\s)]+)\)',
            lambda m: '' if redundant(m[1]) else m[0], line,
        )
        line = re.sub(
            r'<(https?://[^\s>]+)>',
            lambda m: '' if redundant(m[1]) else m[0], line,
        )
        line = re.sub(
            r'https?://[^\s<>]+',
            lambda m: '' if redundant(m[0].rstrip('.,;!')) else m[0], line,
        )
        if re.fullmatch(
            r'\s*(?:[-*+]\s*)?(?:(?:Release page|Web flasher|Firmware(?:\s*\([^)]*\))?|Bundle)\s*:)?\s*',
            line, re.IGNORECASE,
        ):
            if not line.strip():
                lines.append('')
            continue
        lines.append(line.rstrip())
    return re.sub(r'\n{3,}', '\n\n', '\n'.join(lines)).strip()


def build_payload(version, release_url, flasher_url, body):
    content = f'M5MonsterC5-Tab5 {version}\nRelease page: {release_url}\nWeb flasher: {flasher_url}'
    # Count UTF-16 units conservatively, including emoji as two units.
    def length(text):
        return len(text.encode('utf-16-le')) // 2

    if length(content) > 2000:
        raise ValueError('Discord header exceeds 2000 characters')
    description = clean_description(body, release_url, flasher_url)
    if description:
        room = 2000 - length(content) - 2
        if length(description) > room:
            suffix = '\n... More in the release.'
            budget = room - length(suffix)
            if budget < 0:
                raise ValueError('Discord header leaves no room for truncation notice')
            # Slice by Python Unicode code points, never bytes or surrogates.
            end = 0
            for char in description:
                budget -= length(char)
                if budget < 0:
                    break
                end += 1
            description = description[:end].rstrip() + suffix
        content += '\n\n' + description
    return {'content': content, 'allowed_mentions': {'parse': []}}


if __name__ == '__main__':
    payload = build_payload(
        os.environ['FW_VERSION'], os.environ['RELEASE_PAGE'],
        os.environ['PAGES_LINK'], sys.stdin.read(),
    )
    print(json.dumps(payload, ensure_ascii=True))
