import pandas

def hamming_distance(str1: str, str2: str, length: int) -> int:
    """
    Calculates the Hamming distance between two strings up to a specified length.
    """
    if len(str1) < length or len(str2) < length:
        raise ValueError('Strings must have a length of at least {n}'.format(n=length))

    return sum(ch1 != ch2 for ch1, ch2 in zip(str1[:length], str2[:length]))


def cluster_strings(strings: list[str], req_match_len: int, mm_allowed: int) -> list[list[str]]:
    """
    Clusters a list of strings based on a mismatch in the first 10 letters.
    """
    clusters = list()
    for string in strings:
        # Check if the string belongs to any existing cluster
        found_cluster = False
        for cluster in clusters:
            for existing_string in cluster:
                if string[req_match_len:] == existing_string[req_match_len:] and hamming_distance(string, existing_string, req_match_len) < mm_allowed:
                    # Add the string to the existing cluster
                    cluster.append(string)
                    found_cluster = True
                    break
            if found_cluster:
                break

        if not found_cluster:
            # Create a new cluster for the string
            clusters.append([string])

    return clusters


def cluster_solution(solution_path: str, req_match_len: int, mm_allowed: int):
    print('Clustering guides:')
    print('Guides in the same cluster have an identical sequence for the first {n} nucleotides after the PAM (3\' to 5\').'.format(n=req_match_len))
    print('Guides in the same cluster may mismatch up to {n} nucleotides after the seed region.'.format(n=mm_allowed))
    
    df = pandas.read_csv(solution_path)
    df['cluster'] = 0
    seqs = df.sequence.unique().tolist()

    clusters = cluster_strings(seqs, req_match_len, mm_allowed)

    for idx, cluster in enumerate(clusters):
        df.loc[df.sequence.isin(cluster), 'cluster'] = idx

    df.to_csv(solution_path, index=False)
    print('Done clustering. Added a new column \'cluster\' to', solution_path)
    print('The guide RNA set contains {n} clusters.'.format(n=len(clusters)))