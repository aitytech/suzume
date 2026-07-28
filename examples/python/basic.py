"""Minimal Python consumer of the installed Suzume package."""

from suzume import Suzume


def main() -> None:
    with Suzume() as analyzer:
        result = analyzer.analyze_with_normalized_text("東京でりんごを食べる")
    if not result.morphemes:
        raise RuntimeError("analysis returned no morphemes")
    for morpheme in result.morphemes:
        print(
            f"{morpheme.surface}\t{morpheme.pos}\t{morpheme.base_form}"
            f"\t{morpheme.start}\t{morpheme.end}"
        )


if __name__ == "__main__":
    main()
