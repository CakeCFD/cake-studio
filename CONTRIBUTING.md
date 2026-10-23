# Contributing to Cake Studio

Bug reports, questions and pull requests are welcome. Please open an issue first for larger changes so the approach can be agreed before work starts.

## Reporting bugs

Open an issue with the steps to reproduce, what you expected, what happened, and your platform (distribution, architecture, OpenFOAM version). Attach the case or log where possible.

## Pull requests

1. Fork the repository and branch from master.
2. Keep each pull request focused on one change.
3. Build locally with cmake -B build -DCMAKE_BUILD_TYPE=Release and cmake --build build.
4. Open the pull request and fill in the template.

Every pull request is built on x86_64 and aarch64. Areas are labelled automatically from the files touched.

## Numerical changes

Changes to the schemes must keep the validation cases passing and update their recorded results. State the measured effect on accuracy in the pull request.

## Licence

Contributions are accepted under the GNU General Public License v3.0 or later.
