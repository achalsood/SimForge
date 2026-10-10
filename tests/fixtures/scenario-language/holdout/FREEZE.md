# Commit 23 holdout freeze

48 separately authored cases (28 positive, 20 rejection cases).
Frozen before conversational recognition changes or holdout evaluation.
SHA-256 of corpus.json: `eb1a53c55fff3020e35535ab5ed59e9a38a24c3829d7a9964fbc7bbd596aab82`.

This is a disjoint, separately authored evaluation set, not an independent
human-author study. Do not tune grammar from its results. Preserve initial
results and author a new holdout for any later iteration. Coverage gate set
before evaluation: at least 50% exact positive matches, all rejection cases
rejected, zero unsafe acceptances or accepted silent omissions.

Recognition files frozen for first evaluation after development tests passed:

Hashes describe UTF-8 text with LF newlines (normalize CRLF after a Windows checkout).

- `language_numeric.cpp`: `2546ed662f8e346dc366df8c694357a91cb89e89aaa664f8dab3d7165397dfbf`
- `language_recognition.cpp`: `6d4163d64356f7d6d11e77185729fb9346d7065f1db90f5538d18826ee149d61`
- `language_phrases.hpp`: `82bf7f891b6c28ab40eadb39d7eb84fce78f8906daf1b70118c351d752638bf9`
- `language_phrases.cpp`: `e0a54ecc4e148f010f47a7622d8047a6d3c70f209a09b02fd5b1e2f26f8fcdd6`
