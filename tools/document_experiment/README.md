# 문서 분류 실험

NookBit의 기존 규칙 엔진과 로컬 TF-IDF를 같은 문서에서 비교하는 도구입니다. 선택한 문서를 읽고 추천 결과를 CSV로 기록합니다.

## 실행

NookBit의 Release 프리셋이 구성된 x64 개발자 터미널에서 실행합니다. Qt 경로는 설치 위치에 맞춥니다.

```powershell
cmake --build --preset release --target nookbit_document_experiment
$env:PATH = "C:\Qt\6.11.2\msvc2022_64\bin;$env:PATH"
.\out\build\release\src\nookbit_document_experiment.exe tools/document_experiment/sample.json schemas/rules-tree-v3.schema.json out/document-experiment.csv
```

CSV는 새 경로를 지정해야 합니다. 예제는 가상 문서 10개로 동작을 확인하는 용도입니다.

## 내 문서로 비교

`sample.json`을 복사해 실험 목록을 작성합니다. `rules`에 기존 v3 규칙 JSON을 지정하고 `candidates`에 후보 폴더와 설명을 적습니다. `documents`에는 UTF-8 TXT·Markdown 파일 경로와 정답 폴더를 적고 예제의 `text` 필드를 지웁니다. 정답을 보류로 지정할 때는 `expected`에 `null`을 씁니다. 상대 경로는 목록 파일 기준입니다.

- `rules`는 NookBit의 첫 일치 규칙을 적용합니다. 목적지 문자열을 후보 폴더 및 정답과 같은 형태로 씁니다.
- `tfidf_filename`과 `tfidf_body`는 동일한 후보 설명을 사용합니다. 본문 방식은 파일명에 본문 앞 4,000자를 더합니다. 문자 2~3개 조각의 TF-IDF 코사인 유사도로 비교하고, 양의 유사도가 없으면 보류합니다.
- 요약에는 Top-1·Top-3, 추천 건수, 보류가 정답인데 추천한 건수, 입력 준비·방법별 초기화·추론 시간이 표시됩니다. 보류가 정답인 문서는 빈 추천을 정답으로 계산합니다.

샘플 점수는 실제 분류 성능의 근거로 쓰기 어렵습니다. 실제 문서 평가에서는 후보 폴더 설명을 먼저 고정하고 별도로 모은 문서에서 점수를 비교합니다.

## CPU Transformer 비교

같은 목록 파일을 `transformer.py`에 전달하면 Windows에서 E5 모델의 추천과 시간을 측정할 수 있습니다. 공개 모델 파일을 한 번 내려받은 뒤에는 입력 문서와 추론이 로컬에서 처리됩니다.

```powershell
python -m venv out/document-transformer/venv
$python = 'out/document-transformer/venv/Scripts/python.exe'
& $python -m pip install 'numpy==2.2.6' 'onnxruntime==1.23.2' 'tokenizers==0.22.1'
$model = 'out/document-transformer/e5-small'
New-Item -ItemType Directory -Force $model | Out-Null
$source = 'https://huggingface.co/intfloat/multilingual-e5-small/resolve/main/onnx'
curl.exe --fail --location "$source/model.onnx" --output "$model/model.onnx"
curl.exe --fail --location "$source/tokenizer.json" --output "$model/tokenizer.json"
& $python tools/document_experiment/transformer.py tools/document_experiment/sample.json $model out/document-transformer/sample-result.csv --mask-filenames
```

[E5 모델 카드](https://huggingface.co/intfloat/multilingual-e5-small)의 FP32 모델을 사용합니다. 기본 CPU 스레드는 4개이며, 입력은 최대 512토큰입니다. 후보 폴더 벡터를 한 번 준비한 뒤 문서를 한 건씩 처리합니다. `--mask-filenames`는 입력에만 중립적인 파일명을 적용합니다.

CSV는 문서별 추천 순위와 처리 시간을 저장합니다. 실행 요약에는 입력 읽기, 토크나이저·모델 로딩, 후보 준비, 전체 분류 시간과 프로세스 최대 작업 집합이 나옵니다. 처리 시간은 Python 모듈 초기 로딩을 마친 뒤부터 재며, 프로그램 시작까지 포함하려면 새 프로세스의 실행 시간을 함께 잽니다.

기본은 추천 순위를 확인하기 위해 모든 문서에 후보를 반환합니다. `--min-score`로 보류 기준을 지정할 수 있으며, 이 점수는 확률이 아닙니다. 실제 품질을 비교할 때는 별도 검증 문서에서 기준을 정한 뒤 평가 문서에 적용합니다.
